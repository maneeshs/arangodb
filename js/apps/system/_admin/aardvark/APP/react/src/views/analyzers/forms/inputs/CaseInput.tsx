import React, { ChangeEvent } from "react";
import { FormProps } from "../../constants";
import { get } from "lodash";
import Select from "../../../../components/pure-css/form/Select";

type CaseInputProps = FormProps & {
  defaultValue?: string;
};

const CaseInput = ({ formState, dispatch, disabled, defaultValue = 'none' }: CaseInputProps) => {
  const updateCase = (event: ChangeEvent<HTMLSelectElement>) => {
    dispatch({
      type: 'setField',
      field: {
        path: 'properties.case',
        value: event.target.value
      }
    });
  };

  return <Select label={'Case'} value={get(formState, 'properties.case', defaultValue)}
                 onChange={updateCase} disabled={disabled}>
    <option value={'lower'}>Lower</option>
    <option value={'upper'}>Upper</option>
    <option value={'none'}>None</option>
  </Select>;
};

export default CaseInput;
