import React from "react";
import {
  Alert,
  AlertActionCloseButton,
  ClipboardCopy
} from "@patternfly/react-core";

class AlertSaved extends React.Component {
  constructor(props) {
    super(props);
    this.state = {};
  }
  render() {
    return (
      <Alert
        className="over-alert"
        variant="success"
        title="Success"
        action={<AlertActionCloseButton onClose={this.props.handelHideAlert} />}
      >
        Network saved.{" "}
        <ClipboardCopy
          className="state-copy"
          onClick={(event, text) => {
            const clipboard = event.currentTarget.parentElement;
            const el = document.createElement("input");
            el.value = JSON.stringify(this.props.networkYaml);
            clipboard.appendChild(el);
            el.select();
            document.execCommand("copy");
            clipboard.removeChild(el);
          }}
        />
      </Alert>
    );
  }
}

export default AlertSaved;
