/** @jsx h */
import { Component, h } from 'preact';

class SummaryView extends Component {
  render() {
    return (
      <div className="summary-view">
        <h3>Error message</h3>
        <p>
          {this.props.spec.error.summary}
        </p>

        <h3>Learn more</h3>
        <ul>
          <li>
            <a href="#" onClick={this.props.problemDescriptionButtonClicked}>
              Learn what this error means
            </a>
          </li>
          <li>
            <a href="#" onClick={this.props.solutionDescriptionButtonClicked}>
              Learn how to solve this error
            </a>
          </li>
        </ul>

        <h3>Additional information</h3>
        <p>Error ID: {this.props.spec.error.id}</p>
        <pre>{this.props.spec.error.aux_details}</pre>
      </div>
    );
  }
}

export default SummaryView;
