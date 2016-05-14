/** @jsx h */
import { Component, h } from 'preact';
import Tabs from './Tabs.jsx';
import Tab from './Tab.jsx';
import SystemComponentsView from './SystemComponentsView.jsx';
import SummaryView from './SummaryView.jsx';
import ProblemDescriptionView from './ProblemDescriptionView.jsx';
import SolutionDescriptionView from './SolutionDescriptionView.jsx';
import DetailsView from './DetailsView.jsx';

class PageMain extends Component {
  constructor() {
    super();
    this.state = { activeTab: 'summary' };
  }

  render() {
    return (
      <div>
        <div className="page-title-container container">
          <h1 className="page-title">Error starting web application</h1>
        </div>

        <div className="page-system-components-container">
          <div class="container">
            <SystemComponentsView spec={this.props.spec} />
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
            <Tab eventKey="details" title="Deep diagnostics">
              <p />
              <DetailsView spec={this.props.spec} />
            </Tab>
          </Tabs>
        </div>
      </div>
    );
  }

  handleProblemDescriptionButtonClick(e) {
    e.preventDefault();
    this.tabs.setActiveKey('problem-description');
  }

  handleSolutionDescriptionButtonClick(e) {
    e.preventDefault();
    this.tabs.setActiveKey('solution-description');
  }
}

export default PageMain;
