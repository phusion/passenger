/** @jsx h */
import { Component, h } from 'preact';

class ProcessDetailsView extends Component {
  render() {
    return (
      <div>
        <dl>
          {this.renderBeforeItems()}

          <dt>User and group</dt>
          <dd><pre>{this.props.spec.user_info}</pre></dd>

          <dt>Ulimits</dt>
          <dd><pre>{this.props.spec.ulimits}</pre></dd>

          <dt>Environment variables</dt>
          <dd><pre>{this.props.spec.envvars}</pre></dd>

          {this.renderAfterItems()}
        </dl>
      </div>
    );
  }

  renderBeforeItems() {
    var result = [];
    if (this.props.spec.pid) {
      result.push(<dt key="pid-header">PID</dt>);
      result.push(<dd key="pid-content">{this.props.spec.pid}</dd>);
    }
    if (this.props.spec.stdout_and_err) {
      result.push(<dt key="stdout-and-err-header">Stdout and stderr output</dt>);
      result.push(<dd key="stdout-and-err-content"><pre>{this.props.spec.stdout_and_err}</pre></dd>);
    }
    if (this.props.spec.backtrace) {
      result.push(<dt key="backtrace-header">Backtrace</dt>);
      result.push(<dd key="backtrace-content"><pre>{this.props.spec.backtrace}</pre></dd>);
    }
    return result;
  }

  renderAfterItems() {
    var result = [];
    if (this.props.spec.annotations) {
      for (var key of Object.keys(this.props.spec.annotations)) {
        var value = this.props.spec.annotations[key];
        result.push(<dt key={key}>{key}</dt>);
        result.push(<dd key={key + '-value'}><pre>{value}</pre></dd>);
      }
    }
    return result;
  }
}

export default ProcessDetailsView;
