/** @jsx h */
import { Component, h } from 'preact';
import SystemComponentView from './SystemComponentView.jsx';
import './SystemComponentsView.css';

class SystemComponentsView extends Component {
  render() {
    if (this.props.collapsed) {
      return this.renderCollapsed();
    } else {
      return this.renderExpanded();
    }
  }

  renderCollapsed() {
    return (
      <div className="system-components collapsed">
        ...
      </div>
    );
  }

  renderExpanded() {
    return (
      <div className="system-components row">
        <div className="col-sm-3">
          <SystemComponentView
            type="APP_SERVER"
            status="WORKING">
              {this.props.spec.short_program_name}
              {' '}
              <br class="hidden-xs" />
              application server
          </SystemComponentView>
        </div>

        <div className="col-sm-1">
          {this.buildDivider()}
        </div>

        <div className="col-sm-4">
          <SystemComponentView
            type="PREPARATION_WORK"
            status={this.getPreparationWorkStatus()}>
              Preparation work
              {' '}
              <br class="hidden-xs" />
              before executing the app
          </SystemComponentView>
        </div>

        <div className="col-sm-1">
          {this.buildDivider()}
        </div>

        <div className="col-sm-3">
          <SystemComponentView
            type="APP"
            status={this.getWebAppStatus()}>
              Web
              {' '}
              <br class="hidden-xs" />
              application
          </SystemComponentView>
        </div>
      </div>
    );
  }

  /**
   * Returns the general system status. Only two results are possible:
   * 'preparation-error': preparation error, app not reached
   * 'app-error': preparation done, app error
   */
  getGeneralSystemStatus() {
    const journey = this.props.spec.journey;
    if (journey.steps.SUBPROCESS_APP_LOAD_OR_EXEC
     && journey.steps.SUBPROCESS_APP_LOAD_OR_EXEC.state === 'STEP_ERRORED')
    {
      return 'app-error';
    }
    if (journey.steps.SUBPROCESS_LISTEN
     && journey.steps.SUBPROCESS_LISTEN.state === 'STEP_ERRORED')
    {
      return 'app-error';
    }
    return 'preparation-error';
  }

  getPreparationWorkStatus() {
    if (this.getGeneralSystemStatus() === 'app-error') {
      return 'DONE';
    } else {
      return 'ERROR';
    }
  }

  getWebAppStatus() {
    if (this.getGeneralSystemStatus() === 'app-error') {
      return 'ERROR';
    } else {
      return 'NOT_REACHED';
    }
  }

  buildDivider() {
    return (
      <div className="divider">
        <span className="glyphicon glyphicon-menu-right hidden-xs" aria-hidden="true"></span>
        <div className="visible-xs"></div>
      </div>
    )
  }
}

export default SystemComponentsView;
