/** @jsx h */
import { Component, h } from 'preact';
import './SolutionDescriptionView.css';

class SolutionDescriptionView extends Component {
  render() {
    return (
      <div className="solution-description">
        <div dangerouslySetInnerHTML={{ __html: this.props.spec.error.solution_description_html }} />
      </div>
    );
  }
}

export default SolutionDescriptionView;
