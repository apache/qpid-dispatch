import React from "react";
import FieldDetails from "./field-details";
import { RouterStates } from "./nodes";
import EmptySelection from "./empty-selection";

class TopologyContext extends React.Component {
  constructor(props) {
    super(props);
    this.state = {};

    this.contexts = {
      interior: {
        title: "Namespace",
        fields: [
          { title: "Name", type: "text", isRequired: true },
          {
            title: "State",
            type: "states",
            options: RouterStates
          },
          {
            title: "Type",
            type: "radio",
            options: ["Kube", "okd", "OC 3.11", "OC 4.1", "unknown"]
          },
          { title: "Route-suffix", type: "text" },
          { title: "Namespace", type: "text" }
        ],
        actions: [
          {
            title: "Delete",
            onClick: this.props.handleDeleteRouter,
            confirm: true
          }
        ]
      },
      edgeClass: {
        title: "Edge class",
        fields: [{ title: "Name", type: "text", isRequired: true }],
        actions: [
          {
            title: "Delete",
            onClick: this.props.handleDeleteRouter,
            confirm: true
          }
        ],
        extra: { title: "Edge namespaces", type: "edgeTable" }
      },
      edge: {
        title: "Edge namespace",
        fields: [{ title: "Name", type: "text", isRequired: true }],
        actions: [
          {
            title: "Delete",
            onClick: this.props.handleDeleteRouter,
            confirm: true
          }
        ]
      },
      connector: {
        title: "Connection",
        fields: [
          { title: "connector type", type: "label" },
          { title: "connector", type: "label" },
          { title: "listener", type: "label" }
        ],
        actions: [
          {
            title: "Delete",
            onClick: this.props.handleDeleteConnection,
            confirm: true
          },
          {
            title: "Reverse",
            onClick: this.props.handleReverseConnection,
            isDisabled: this.isActionDisabled
          }
        ]
      }
    };
  }

  isActionDisabled = (title, networkInfo, selectedKey) => {
    if (title === "Reverse") {
      const currentLink = networkInfo.links.find(l => l.key === selectedKey);
      if (currentLink) {
        if (currentLink["connector type"] === "edge") return true;
      }
    }
    return false;
  };

  isRequired = (title, networkInfo, selectedKey) => {
    // if there are any links going to this node, suffix and namespace are required
    if (title === "Route-suffix" || title === "Namespace")
      return networkInfo.links.some(l => l.source.key === selectedKey);
    return false;
  };

  render() {
    let currentContext = null;
    const currentNode = this.props.networkInfo.nodes.find(
      n => n.key === this.props.selectedKey
    );
    if (currentNode) {
      currentContext = this.contexts[currentNode.type];
    } else {
      const currentLink = this.props.networkInfo.links.find(
        l => l.key === this.props.selectedKey
      );
      if (currentLink) {
        currentContext = this.contexts[currentLink.type];
      }
    }

    if (!currentContext) {
      return (
        <div>
          <EmptySelection />
        </div>
      );
    }
    return (
      <FieldDetails
        details={currentContext}
        networkInfo={this.props.networkInfo}
        selectedKey={this.props.selectedKey}
        handleEditField={this.props.handleEditField}
        handleAddEdge={this.props.handleAddEdge}
        handleDeleteEdge={this.props.handleDeleteEdge}
        handleEdgeNameChange={this.props.handleEdgeNameChange}
        handleSelectEdgeRow={this.props.handleSelectEdgeRow}
        handleRadioChange={this.props.handleRadioChange}
      />
    );
  }
}

export default TopologyContext;
