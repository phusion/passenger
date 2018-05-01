/** @jsx h */
import { Component, h } from 'preact';

class Tab extends Component {
  render() {
    return (
      <div>{this.props.children}</div>
    );
  }
}

export default Tab;
