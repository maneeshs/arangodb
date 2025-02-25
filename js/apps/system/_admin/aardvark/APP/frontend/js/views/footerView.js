/* jshint browser: true */
/* jshint unused: false */
/* global _, Backbone, frontendConfig, document, templateEngine, $, arangoHelper, window, Noty */

(function () {
  'use strict';
  window.FooterView = Backbone.View.extend({
    el: '#footerBar',
    system: {},
    isOffline: true,
    isOfflineCounter: 0,
    firstLogin: true,
    timer: 15000,
    lap: 0,
    timerFunction: null,
    // last time of JWT renewal call, in seconds.
    // this is initialized to the current time so we don't 
    // fire off a renewal request at the very beginning.
    lastTokenRenewal: Date.now() / 1000, 

    events: {
      'click .footer-center p': 'showShortcutModal'
    },

    initialize: function () {
      // also server online check
      var self = this;
      window.setInterval(function () {
        self.getVersion();
      }, self.timer);
      self.getVersion();

      window.VISIBLE = true;
      document.addEventListener('visibilitychange', function () {
        window.VISIBLE = !window.VISIBLE;
      });

      $('#offlinePlaceholder button').on('click', function () {
        self.getVersion();
      });

      window.setTimeout(function () {
        if (window.frontendConfig.isCluster === true) {
          $('.health-state').css('cursor', 'pointer');
          $('.health-state').on('click', function () {
            window.App.navigate('#nodes', {trigger: true});
          });
        }
      }, 1000);

      // track an activity once when we initialize this view
      arangoHelper.noteActivity();
      
      window.setInterval(function () {
        if (self.isOffline) {
          // only try to renew token if we are still online
          return;
        }
        
        var frac = (frontendConfig.sessionTimeout >= 1800) ? 0.95 : 0.8;
        // threshold for renewal: once session is x% over
        var renewalThreshold = frontendConfig.sessionTimeout * frac;

        var now = Date.now() / 1000;
        var lastActivity = arangoHelper.lastActivity();

        // seconds in which the last user activity counts as significant
        var lastSignificantActivityTimePeriod = 90 * 60;

        // if this is more than the renewal threshold, limit it
        if (lastSignificantActivityTimePeriod > renewalThreshold * 0.95) {
          lastSignificantActivityTimePeriod = renewalThreshold * 0.95;
        }

        if (lastActivity > 0 && (now - lastActivity) > lastSignificantActivityTimePeriod) {
          // don't make an attempt to renew the token if last 
          // user activity is longer than 90 minutes ago
          return;
        }

        // to save some superfluous HTTP requests to the server, 
        // try to renew only if session time is x% or more over
        if (now - self.lastTokenRenewal < renewalThreshold) {
          return;
        }

        arangoHelper.renewJwt(function() {
          // successful renewal of token. now store last renewal time so
          // that we later only renew if the session is again about to
          // expire
          self.lastTokenRenewal = now;
        });
      }, 15 * 1000);
    },

    template: templateEngine.createTemplate('footerView.ejs'),

    showServerStatus: function (isOnline) {
      if (!window.App.isCluster) {
        if (isOnline === true) {
          $('#healthStatus').removeClass('negative');
          $('#healthStatus').addClass('positive');
          $('.health-state').html('GOOD');
          $('.health-icon').html('<i class="fa fa-check-circle"></i>');
          $('#offlinePlaceholder').hide();
        } else {
          $('#healthStatus').removeClass('positive');
          $('#healthStatus').addClass('negative');
          $('.health-state').html('UNKNOWN');
          $('.health-icon').html('<i class="fa fa-exclamation-circle"></i>');

          // remove modals if visible
          window.modalView.hide();

          // show offline overlay
          $('#offlinePlaceholder').show();

          // remove error messages
          Noty.clearQueue();
          Noty.closeAll();

          this.reconnectAnimation(0);
        }
      } else {
        this.renderClusterState(isOnline);
      }
    },

    reconnectAnimation: function (lap) {
      var self = this;

      if (lap === 0) {
        self.lap = lap;
        $('#offlineSeconds').text(self.timer / 1000);
        clearTimeout(self.timerFunction);
      }

      if (self.lap < this.timer / 1000) {
        self.lap++;
        $('#offlineSeconds').text(self.timer / 1000 - self.lap);

        self.timerFunction = window.setTimeout(function () {
          if (self.timer / 1000 - self.lap === 0) {
            self.getVersion();
          } else {
            self.reconnectAnimation(self.lap);
          }
        }, 1000);
      }
    },

    renderClusterState: function (connection) {
      if (connection) {
        $('#offlinePlaceholder').hide();

        var callbackFunction = function (data) {
          window.clusterHealth = data.Health;

          var error = 0;

          if (Object.keys(window.clusterHealth).length !== 0) {
            _.each(window.clusterHealth, function (node) {
              if (node.Role === 'DBServer' || node.Role === 'Coordinator') {
                if (node.Status !== 'GOOD') {
                  error++;
                }
              }
            });

            if (error > 0) {
              $('#healthStatus').removeClass('positive');
              $('#healthStatus').addClass('negative');
              $('.health-state').html(error + ' NODE(S) ERROR');
              $('.health-icon').html('<i class="fa fa-exclamation-circle"></i>');
            } else {
              $('#healthStatus').removeClass('negative');
              $('#healthStatus').addClass('positive');
              $('.health-state').html('NODES OK');
              $('.health-icon').html('<i class="fa fa-check-circle"></i>');
            }
          } else {
            $('.health-state').html('HEALTH ERROR');
            $('#healthStatus').removeClass('positive');
            $('#healthStatus').addClass('negative');
            $('.health-icon').html('<i class="fa fa-exclamation-circle"></i>');
          }
        };

        if (frontendConfig.clusterApiJwtPolicy !== 'jwt-all') {
          // check cluster state
          $.ajax({
            type: 'GET',
            cache: false,
            url: arangoHelper.databaseUrl('/_admin/cluster/health'),
            contentType: 'application/json',
            processData: false,
            async: true,
            success: function (data) {
              if (window.App) {
                window.App.lastHealthCheckResult = data;
              }
              callbackFunction(data);
              // notify NodesView about new health data
              if (window.location.hash === '#nodes' && window.App && window.App.nodesView) {
                window.App.nodesView.render(false);
              }
            },
            error: function () {
              if (window.App) {
                window.App.lastHealthCheckResult = null;
              }
            }
          });
        }
      } else {
        $('#healthStatus').removeClass('positive');
        $('#healthStatus').addClass('negative');
        $('.health-state').html(window.location.host + ' OFFLINE');
        $('.health-icon').html('<i class="fa fa-exclamation-circle"></i>');

        // show offline overlay
        $('#offlinePlaceholder').show();
        this.reconnectAnimation(0);
      }
    },

    showShortcutModal: function () {
      window.arangoHelper.hotkeysFunctions.showHotkeysModal();
    },

    getVersion: function () {
      var self = this;

      // always retry this call, because it also checks if the server is online
      $.ajax({
        type: 'GET',
        cache: false,
        url: arangoHelper.databaseUrl('/_api/version'),
        contentType: 'application/json',
        processData: false,
        async: true,
        success: function (data) {
          frontendConfig.version = data;
          if (!frontendConfig.version.hasOwnProperty('version')) {
            frontendConfig.version.version = 'N/A';
          }
          self.showServerStatus(true);
          if (self.isOffline === true) {
            self.isOffline = false;
            self.isOfflineCounter = 0;
            if (!self.firstLogin) {
              window.setTimeout(function () {
                self.showServerStatus(true);
              }, 1000);
            } else {
              self.firstLogin = false;
            }
            self.system.name = data.server;
            self.system.version = data.version;
            self.render();
          }
        },
        error: function (jqXHR) {
          if (jqXHR.status === 401) {
            self.showServerStatus(true);
            window.App.navigate('login', {trigger: true});
          } else {
            self.isOffline = true;
            self.isOfflineCounter++;
            if (self.isOfflineCounter >= 1) {
              // arangoHelper.arangoError("Server", "Server is offline")
              self.showServerStatus(false);
            }
          }
        }
      });

      if (!self.system.hasOwnProperty('database')) {
        $.ajax({
          type: 'GET',
          cache: false,
          url: arangoHelper.databaseUrl('/_api/database/current'),
          contentType: 'application/json',
          processData: false,
          async: true,
          success: function (data) {
            var name = data.result.name;
            self.system.database = name;

            var timer = window.setInterval(function () {
              var navElement = $('#databaseNavi');

              if (navElement) {
                window.clearTimeout(timer);
                timer = null;
                self.render();
              }
            }, 50);
          }
        });
      }
    },

    renderVersion: function () {
      if (this.system.hasOwnProperty('database') && this.system.hasOwnProperty('name')) {
        $(this.el).html(this.template.render({
          name: this.system.name,
          version: this.system.version,
          database: this.system.database
        }));
      }
    },

    render: function () {
      if (!this.system.version) {
        this.getVersion();
      }
      $(this.el).html(this.template.render({
        name: this.system.name,
        version: this.system.version
      }));
      return this;
    }

  });
}());
