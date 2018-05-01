/** @jsx h */
import { Component, h } from 'preact';
import Tabs from './Tabs.jsx';
import Tab from './Tab.jsx';
import JourneyView from './JourneyView.jsx';
import ProcessDetailsView from './ProcessDetailsView.jsx';

class DetailsView extends Component {
  render() {
    return (
      <div className="details-view">
        <p>Error ID: {this.props.spec.error.id}</p>
        <Tabs defaultActiveKey="problem-location" id="details-process-tabs">
          <Tab eventKey="problem-location" title="Problem location">
            <p />
            <JourneyView spec={this.props.spec} />
          </Tab>

          <Tab eventKey="core-process" title={this.props.spec.short_program_name + ' core'}>
            <p />
            <ProcessDetailsView spec={this.props.spec.diagnostics.core_process} />
          </Tab>

          {this.maybeRenderPreloaderProcessDetailsTab()}

          <Tab eventKey="subprocess" title="Subprocess">
            <p />
            <ProcessDetailsView spec={this.props.spec.diagnostics.subprocess} />
          </Tab>

          <Tab eventKey="system-wide" title="System-wide stats">
            <p />
            <pre>{this.props.spec.diagnostics.system_wide.system_metrics}</pre>
          </Tab>
        </Tabs>
      </div>
    );
  }

  maybeRenderPreloaderProcessDetailsTab() {
    if (this.props.spec.journey.type === 'SPAWN_THROUGH_PRELOADER') {
      return (
        <Tab eventKey="preloader" title="Preloader process">
          <p />
          <ProcessDetailsView spec={this.props.spec.diagnostics.preloader_process} />
        </Tab>
      );
    }
  }
}

export default DetailsView;
