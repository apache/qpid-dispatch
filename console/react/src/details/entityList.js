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
import { List, ListItem } from "@patternfly/react-core";

class EntityList extends React.Component {
  constructor(props) {
    super(props);
    this.state = { entity: null };
    this.exclude = [
      "dummy",
      "configurationEntity",
      "entity",
      "management",
      "operationalEntity",
      "org.amqp.management"
    ];
    this.cleanList = Object.keys(this.props.schema.entityTypes).filter(
      li => !this.exclude.includes(li)
    );

    this.cleanList.sort();
  }

  handleSelectEntity = event => {
    const entity = event.target.textContent;
    this.setState({ entity });
    this.props.handleSelectEntity(entity);
  };

  render() {
    return (
      <List className="entities-list pf-u-box-shadow-sm-right">
        {this.cleanList.map(entity => (
          <ListItem
            data-testid={entity}
            key={entity}
            onClick={this.handleSelectEntity}
            className={entity === this.state.entity ? "selected" : ""}
          >
            {entity}
          </ListItem>
        ))}
      </List>
    );
  }
}

export default EntityList;
