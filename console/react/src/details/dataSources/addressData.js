/*
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.
*/

import React from "react";
import DefaultData from "./defaultData";
import { utils } from "../../common/amqp/utilities";

const AddressType = ({ value, extraInfo }) => {
  const data = extraInfo.rowData.data;
  const identity = utils.identity_clean(data.identity);
  const cls = utils.addr_class(identity);

  return (
    <span className="entity-type">
      <i className={`address-${cls}`}></i>
      {cls}
    </span>
  );
};

class AddressData extends DefaultData {
  constructor(service, schema) {
    super(service, schema);
    this.typeFormatter = AddressType;
    this.detailName = "router.address";
  }
}

export default AddressData;
