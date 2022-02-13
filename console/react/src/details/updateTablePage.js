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
import { PageSection, PageSectionVariants } from "@patternfly/react-core";
import {
  Alert,
  Button,
  Stack,
  StackItem,
  TextContent,
  Text,
  TextVariants,
  Breadcrumb,
  BreadcrumbItem
} from "@patternfly/react-core";

import {
  Form,
  FormGroup,
  TextInput,
  FormSelectOption,
  FormSelect,
  Checkbox,
  ActionGroup
} from "@patternfly/react-core";

import { cellWidth } from "@patternfly/react-table";
import { Card, CardBody } from "@patternfly/react-core";
import { Navigate } from "react-router-dom";
import { dataMap as detailsDataMap, defaultData } from "./entityData";
import { utils } from "../common/amqp/utilities";

class UpdateTablePage extends React.Component {
  constructor(props) {
    super(props);
    // if we get to this page and we don't have a props.location.state.entity
    // then redirect back to the dashboard.
    // this can happen if we get here from a bookmark or browser refresh
    this.entity =
      this.props.entity ||
      (this.props &&
        this.props.location &&
        this.props.location.state &&
        this.props.location.state.entity);

    if (!this.entity) {
      this.state.redirect = true;
    } else {
      this.dataSource = !detailsDataMap[this.entity]
        ? new defaultData(this.props.service, this.props.schema)
        : new detailsDataMap[this.entity](this.props.service, this.props.schema);
    }

    this.state = {
      columns: [
        { title: "Attribute", transforms: [cellWidth(20)] },
        {
          title: "Value",
          transforms: [cellWidth("max")],
          props: { className: "pf-u-text-align-left" }
        }
      ],
      rows: [],
      redirect: false,
      redirectState: { page: 1 },
      redirectPath: "/dashboard",
      lastUpdated: new Date(),
      changes: false,
      record: this.fixNull(this.props.locationState.currentRecord),
      errorText: null
    };
    this.originalRecord = utils.copy(this.state.record);
  }

  fixNull = rec => {
    const record = utils.copy(rec);
    const attributes = this.dataSource.schemaAttributes(this.entity);
    for (const attr in record) {
      if (record[attr] === null) {
        if (attributes[attr].type === "string") {
          record[attr] = "";
        } else if (attributes[attr].type === "integer") {
          record[attr] = 0;
        }
      }
    }
    return record;
  };

  handleTextInputChange = (value, key) => {
    const { record } = this.state;
    record[key] = value;
    let changes = false;
    for (let attr in record) {
      changes = changes || record[attr] !== this.originalRecord[attr];
    }
    this.setState({ record, changes });
  };

  schemaToForm = () => {
    const record = this.state.record;
    const attributes = this.dataSource.schemaAttributes(this.entity);
    const formGroups = [];
    if (this.state.errorText) {
      formGroups.push(<Alert variant="danger" isInline title={this.state.errorText} />);
    }
    for (let attributeKey in attributes) {
      const attribute = attributes[attributeKey];
      let type = attribute.type;
      let options = [];
      let readOnly = attributeKey === "identity";
      if (type === "list") readOnly = true;
      if (type === "integer" && attribute.graph) readOnly = true;
      let required = attribute.required;
      const value = record[attributeKey];
      if (
        this.dataSource.updateMetaData &&
        this.dataSource.updateMetaData[attributeKey]
      ) {
        const override = this.dataSource.updateMetaData[attributeKey];
        if (override.readOnly) {
          type = "string";
          readOnly = true;
        }
        if (override.type === "select") {
          type = "select";
          options = override.options;
        }
      }
      const id = `form-${attributeKey}`;
      const formGroupProps = {
        label: attributeKey,
        isRequired: required,
        fieldId: id,
        helperText: attribute.description
      };
      if (!readOnly) {
        if (type === "string" || type === "integer") {
          formGroups.push(
            <FormGroup {...formGroupProps} key={attributeKey}>
              <TextInput
                value={record[attributeKey]}
                isRequired={required}
                type={type === "string" ? "text" : "number"}
                id={id}
                aria-describedby="entiy-form-field"
                name={attributeKey}
                isDisabled={readOnly}
                onChange={value => this.handleTextInputChange(value, attributeKey)}
              />
            </FormGroup>
          );
        } else if (type === "select") {
          formGroups.push(
            <FormGroup {...formGroupProps} key={attributeKey}>
              <FormSelect
                value={value}
                onChange={value => this.handleTextInputChange(value, attributeKey)}
                id={id}
                name={attributeKey}
              >
                {options.map((option, index) => (
                  <FormSelectOption
                    isDisabled={false}
                    key={`${attributeKey}-${index}`}
                    value={option}
                    label={option}
                  />
                ))}
              </FormSelect>
            </FormGroup>
          );
        } else if (type === "boolean") {
          formGroups.push(
            <FormGroup {...formGroupProps} key={attributeKey}>
              <Checkbox
                isChecked={record[attributeKey] === null ? false : record[attributeKey]}
                onChange={value => this.handleTextInputChange(value, attributeKey)}
                label={attributeKey}
                id={id}
                name={attributeKey}
              />
            </FormGroup>
          );
        }
      }
    }
    return formGroups;
  };

  toString = val => {
    return val === null ? "" : String(val);
  };

  icap = s => s.charAt(0).toUpperCase() + s.slice(1);

  parentItem = () => this.state.record.name;

  breadcrumbSelected = () => {
    this.props.handleSelectEntity(this.entity);
  };

  handleCancel = () => {
    this.props.handleActionCancel(this.props);
  };

  handleUpdate = () => {
    const record = this.state.record;
    const attributes = {};
    // identity is needed to update the record
    attributes["identity"] = record.identity;
    const schemaAttributes = this.dataSource.schemaAttributes(this.entity);
    // pass any other attributes that have changed
    for (const attr in record) {
      if (record[attr] !== this.originalRecord[attr]) {
        attributes[attr] = record[attr];
      }
      if (schemaAttributes[attr] && schemaAttributes[attr].required) {
        attributes[attr] = record[attr];
      }
      if (attr === "outputFile") {
        attributes["outputFile"] = record.outputFile === "" ? null : record.outputFile;
      }
    }
    const { validated, errorText } = this.dataSource.validate(record);
    if (!validated) {
      this.setState({ errorText });
      return;
    }
    // call update
    this.props.service.management.connection
      .sendMethod(record.routerId || record.nodeId, this.entity, attributes, "UPDATE")
      .then(results => {
        let statusCode = results.context.message.application_properties.statusCode;
        if (statusCode < 200 || statusCode >= 300) {
          const msg = `Updated ${record.name} failed with message: ${results.context.message.application_properties.statusDescription}`;
          console.log(`error ${msg}`);
          this.setState({
            errorText: results.context.message.application_properties.statusDescription
          });
          //this.props.handleAddNotification("action", msg, new Date(), "danger");
        } else {
          const msg = `Updated ${this.props.entity} ${record.name}`;
          console.log(`success ${msg}`);
          this.props.handleAddNotification("action", msg, new Date(), "success");
          const props = this.props;
          props.locationState.currentRecord = record;
          this.props.handleActionCancel(props);
        }
      });
  };

  render() {
    if (this.state.redirect) {
      return (
        <Navigate
          to={{
            pathname: this.state.redirectPath,
            state: this.state.redirectState
          }}
        />
      );
    }

    return (
      <React.Fragment>
        <PageSection variant={PageSectionVariants.light} className="overview-table-page">
          <Stack>
            <StackItem className="overview-header details">
              <Breadcrumb>
                <BreadcrumbItem className="link-button" onClick={this.breadcrumbSelected}>
                  {this.icap(this.entity)}
                </BreadcrumbItem>
              </Breadcrumb>

              <TextContent className="details-table-header">
                <Text className="overview-title" component={TextVariants.h1}>
                  {this.parentItem()}
                </Text>
                <ActionGroup>
                  <Button
                    className="detail-action-button link-button"
                    onClick={this.handleCancel}
                  >
                    Cancel
                  </Button>
                  <Button onClick={this.handleUpdate} isDisabled={!this.state.changes}>
                    Update
                  </Button>
                </ActionGroup>
              </TextContent>
            </StackItem>
            <StackItem id="update-form">
              <Card>
                <CardBody>
                  <Form isHorizontal aria-label="update-entity-form">
                    {this.schemaToForm()}
                  </Form>
                </CardBody>
              </Card>
            </StackItem>
          </Stack>
        </PageSection>
      </React.Fragment>
    );
  }
}

export default UpdateTablePage;
