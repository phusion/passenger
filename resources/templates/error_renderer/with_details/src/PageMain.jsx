/** @jsx h */
import { Component, h, render } from 'preact';
import Tabs from './Tabs.jsx';
import Tab from './Tab.jsx';
import SystemComponentsView from './SystemComponentsView.jsx';
import SummaryView from './SummaryView.jsx';
import ProblemDescriptionView from './ProblemDescriptionView.jsx';
import SolutionDescriptionView from './SolutionDescriptionView.jsx';
import GetHelpView from './GetHelpView.jsx';
import DetailsView from './DetailsView.jsx';

class PageMain extends Component {
  constructor() {
    super();
    this.state = { systemComponentsViewCollapsed: false };
    this.Preact = {
      Component: Component,
      h: h,
      render: render
    };
    this.Components = {
      Tabs: Tabs,
      Tab: Tab
    };
    this._extraTabs = [];

    if (window.localStorage) {
      this.state.systemComponentsViewCollapsed =
        window.localStorage.getItem('_passenger_error_page_system_components_collapsed')
        === 'true';
    }
  }

  render() {
    return (
      <div>
        <div className="page-title-container container">
          <h1 className="page-title">Error starting web application</h1>
        </div>

        <div className="page-system-components-container">
          <div className="collapse-button">
            {this._renderCollapseButton()}
          </div>
          <div class="container">
            <SystemComponentsView spec={this.props.spec} collapsed={this.state.systemComponentsViewCollapsed} />
          </div>
        </div>

        <div className="page-main container">
          <Tabs className="page-main-tabs" defaultActiveKey="problem-description" ref={ (x) => { this.tabs = x } }>
            <Tab eventKey="problem-description" title="What happened?">
              <p />
              <ProblemDescriptionView spec={this.props.spec} />
            </Tab>
            <Tab eventKey="solution-description" title="How do I solve this?">
              <p />
              <SolutionDescriptionView spec={this.props.spec} />
            </Tab>
            {this._renderExtraTabs()}
            <Tab eventKey="get-help" title="Get help">
              <p />
              <GetHelpView spec={this.props.spec} />
            </Tab>
            <Tab eventKey="details" title="Detailed diagnostics">
              <p />
              <DetailsView spec={this.props.spec} />
            </Tab>
          </Tabs>
        </div>
        <footer>
            <div>This website is powered by <a href="https://www.phusionpassenger.com"><b>Phusion Passenger</b></a>&reg;, the smart application server built by <b>Phusion</b>&reg;.</div>
        </footer>
      </div>
    );
  }

  addExtraTab(key, title, component) {
    this._extraTabs.push({
      key: key,
      title: title,
      component: component
    });
  }

  setActiveTab(key) {
    this.tabs.setActiveKey(key);
  }


  _renderCollapseButton() {
    if (this.state.systemComponentsViewCollapsed) {
      return (
        <a href="javascript:void(0)" onClick={this._handleExpandSystemComponentsView.bind(this)}>Expand</a>
      );
    } else {
      return (
        <a href="javascript:void(0)" onClick={this._handleCollapseSystemComponentsView.bind(this)}>Collapse</a>
      );
    }
  }

  _renderExtraTabs() {
    return this._extraTabs.map(function(spec) {
      return (
        <Tab eventKey={spec.key} title={spec.title}>
          {spec.component}
        </Tab>
      );
    });
  }

  _handleExpandSystemComponentsView() {
    this.setState({ systemComponentsViewCollapsed: false });
    if (window.localStorage) {
      try {
        window.localStorage.setItem('_passenger_error_page_system_components_collapsed', 'false');
      } catch (e) {
        // Do nothing
      }
    }
  }

  _handleCollapseSystemComponentsView() {
    this.setState({ systemComponentsViewCollapsed: true });
    if (window.localStorage) {
      try {
        window.localStorage.setItem('_passenger_error_page_system_components_collapsed', 'true');
      } catch (e) {
        // Do nothing
      }
    }
  }
}

export default PageMain;
