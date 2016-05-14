/** @jsx h */
import { Component, h } from 'preact';

class ProblemDescriptionView extends Component {
  render() {
    return (
      <div className="problem-description">
        <div dangerouslySetInnerHTML={{ __html: this.props.spec.error.problem_description_html }} />
      </div>
    );
  }
}

export default ProblemDescriptionView;
