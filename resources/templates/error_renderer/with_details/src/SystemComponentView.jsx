/** @jsx h */
import { Component, h } from 'preact';
import './SystemComponentView.css';

class SystemComponentView extends Component {
  render() {
    const labelAndClass = this.getStatusLabelAndClass();
    const statusLabel = labelAndClass[0];
    const statusClass = labelAndClass[1];
    return (
      <div className={'system-component-view ' + statusClass}>
        <div className="icon">{this.getIcon()}</div>
        <div className="name">{this.props.children}</div>
        <div className="status-icon">{this.getStatusIcon()}</div>
        <div className="status-label">{statusLabel}</div>
      </div>
    );
  }

  getIcon() {
    if (this.props.type == 'APP_SERVER') {
      return (<span class="glyphicon glyphicon-plane" aria-hidden="true"></span>);
    } else if (this.props.type == 'PREPARATION_WORK') {
      return (<span class="glyphicon glyphicon-cog" aria-hidden="true"></span>);
    } else if (this.props.type == 'APP') {
      return (<span class="glyphicon glyphicon-tower" aria-hidden="true"></span>);
    } else {
      return (<span class="glyphicon glyphicon-asterisk" aria-hidden="true"></span>);
    }
  }

  getStatusIcon() {
    if (this.props.status === 'WORKING' || this.props.status == 'DONE') {
      return (<span class="glyphicon glyphicon-ok" aria-hidden="true"></span>);
    } else if (this.props.status === 'ERROR') {
      return (<span class="glyphicon glyphicon-remove" aria-hidden="true"></span>);
    } else if (this.props.status === 'NOT_REACHED') {
      return (<span class="glyphicon glyphicon-minus-sign" aria-hidden="true"></span>);
    } else {
      return (<span class="glyphicon glyphicon-question-sign" aria-hidden="true"></span>);
    }
  }

  getStatusLabelAndClass() {
    if (this.props.status === 'WORKING') {
      return ['Working', 'working'];
    } else if (this.props.status === 'DONE') {
      return ['Done', 'done'];
    } else if (this.props.status === 'ERROR') {
      return ['Error', 'error'];
    } else if (this.props.status === 'NOT_REACHED') {
      return ['Not reached', 'not_reached'];
    } else {
      return ['Unknown', 'unknown'];
    }
  }
}

export default SystemComponentView;
