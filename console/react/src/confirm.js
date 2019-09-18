import React from "react";
import { Modal, Button } from "@patternfly/react-core";
import PropTypes from "prop-types";

class Confirm extends React.Component {
  static propTypes = {
    handleConfirm: PropTypes.func.isRequired,
    buttonText: PropTypes.string.isRequired,
    children: PropTypes.object.isRequired,
    title: PropTypes.string.isRequired,
    isDeleteDisabled: PropTypes.bool,
    variant: PropTypes.string
  };

  constructor(props) {
    super(props);
    this.state = {
      isModalOpen: false
    };
  }

  handleModalToggle = () => {
    this.setState({ isModalOpen: !this.state.isModalOpen });
  };

  handleModalConfirm = () => {
    this.props.handleConfirm();
    this.handleModalToggle();
  };
  handleModalCancel = () => {
    this.handleModalToggle();
  };
  render() {
    const { isModalOpen } = this.state;
    const variant = this.props.variant || "primary";
    return (
      <React.Fragment>
        <Button
          isDisabled={this.props.isDeleteDisabled}
          variant={variant}
          onClick={this.handleModalToggle}
        >
          {this.props.buttonText}
        </Button>
        <Modal
          isSmall
          title={this.props.title}
          isOpen={isModalOpen}
          onClose={this.handleModalCancel}
          actions={[
            <Button
              key="cancel"
              variant="secondary"
              onClick={this.handleModalCancel}
            >
              Cancel
            </Button>,
            <Button
              key="confirm"
              variant="primary"
              onClick={this.handleModalConfirm}
            >
              Confirm
            </Button>
          ]}
        >
          {this.props.children}
        </Modal>
      </React.Fragment>
    );
  }
}

export default Confirm;
