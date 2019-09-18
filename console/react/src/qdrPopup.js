import React from "react";

class QDRPopup extends React.Component {
  constructor(props) {
    super(props);
    this.state = {};
  }

  render() {
    return <div dangerouslySetInnerHTML={{ __html: this.props.content }} />;
  }
}

export default QDRPopup;
