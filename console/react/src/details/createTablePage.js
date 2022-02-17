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

class CreateTablePage extends React.Component {
  constructor(props) {
    super(props);
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
      record: {},
      errorText: null
    };

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
      this.props.history.replace("/dashboard");
    } else {
      this.dataSource = !detailsDataMap[this.entity]
        ? new defaultData(this.props.service, this.props.schema)
        : new detailsDataMap[this.entity](this.props.service, this.props.schema);
      this.locationState = this.props.locationState;

      const attributes = this.dataSource.schemaAttributes(this.entity);
      for (let attributeKey in attributes) {
        this.state.record[attributeKey] = this.getDefault(attributes[attributeKey]);
      }
    }
  }

  handleTextInputChange = (value, key) => {
    const { record } = this.state;
    record[key] = value;
    this.setState({ record });
  };

  getDefault = attribute => {
    let defaultVal = "";
    if (attribute.default) defaultVal = attribute.default;
    if (typeof attribute.default === "undefined") {
      if (attribute.type === "boolean") {
        defaultVal = false;
      } else if (Array.isArray(attribute.type)) {
        defaultVal = attribute.type[0];
      } else if (attribute.type === "integer") {
        defaultVal = 0;
      }
    }
    return defaultVal;
  };

  schemaToForm = () => {
    const attributes = this.dataSource.schemaAttributes(this.entity);
    const formGroups = [];
    if (this.state.errorText) {
      formGroups.push(<Alert variant="danger" isInline title={this.state.errorText} />);
    }
    for (let attributeKey in attributes) {
      if (attributeKey !== "identity") {
        const attribute = attributes[attributeKey];
        if (!attribute.graph) {
          let type = attribute.type;
          let options = [];
          let readOnly = attributeKey === "identity";
          if (type === "list") readOnly = true;
          if (Array.isArray(attribute.type)) {
            type = "select";
            options = attribute.type;
          }
          let required = attribute.required;
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
          if (!readOnly) {
            // no need to display readonly fields on create form
            const id = `form-${attributeKey}`;
            const formGroupProps = {
              label: attributeKey,
              isRequired: required,
              fieldId: id,
              helperText: attribute.description
            };
            if (type === "string" || type === "integer") {
              formGroups.push(
                <FormGroup {...formGroupProps} key={attributeKey}>
                  <TextInput
                    value={this.state.record[attributeKey]}
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
                    value={this.state.record[attributeKey]}
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
                    isChecked={this.state.record[attributeKey]}
                    label={attributeKey}
                    id={id}
                    name={attributeKey}
                    onChange={value => this.handleTextInputChange(value, attributeKey)}
                  />
                </FormGroup>
              );
            }
          }
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

  handleCreate = () => {
    const { record } = this.state;
    const attributes = {};
    const schemaAttributes = this.dataSource.schemaAttributes(this.entity);
    for (let attr in record) {
      if (
        this.getDefault(schemaAttributes[attr]) !== record[attr] ||
        schemaAttributes[attr].required
      )
        attributes[attr] = record[attr];
    }

    // call create
    this.props.service.management.connection
      .sendMethod(this.props.routerId, this.entity, attributes, "CREATE")
      .then(results => {
        let statusCode = results.context.message.application_properties.statusCode;
        if (statusCode < 200 || statusCode >= 300) {
          let message = results.context.message.application_properties.statusDescription;
          //const msg = `Create failed with message: ${message}`;
          console.log(
            `error Create failed ${results.context.message.application_properties.statusDescription}`
          );
          //this.props.handleAddNotification("action", msg, new Date(), "danger");
          this.setState({ errorText: message });
        } else {
          const msg = `Created ${this.props.entity} ${record.name}`;
          console.log(`success ${msg}`);
          this.props.handleAddNotification("action", msg, new Date(), "success");
          this.handleCancel();
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
                  <Button onClick={this.handleCreate}>Create</Button>
                </ActionGroup>
              </TextContent>
            </StackItem>
            <StackItem id="update-form">
              <Card>
                <CardBody>
                  <Form isHorizontal aria-label="create-entity-form">
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

export default CreateTablePage;
