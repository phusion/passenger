/** @jsx h */
import { Component, h } from 'preact';

class GetHelpView extends Component {
  render() {
    return (
      <div>
        <p>
          If you are stuck with a problem, please do not hesitate to contact
          one of the support resources.
          {' ' + this.props.spec.program_name + ' '}
          has a friendly community of users who look out for each other.
          We &mdash;
          {" Passenger's "}
          authors &mdash; are also ready to help you whenever we can.
        </p>
        <div className="row">
          <div className="col-sm-4">
            <h2>Community support</h2>
            <p>
              Post a message to Stack Overflow. Support is provided by the community on a
              best-effort basis, so sometimes a bit of patience will help.
            </p>
            <p>
              <a href="https://stackoverflow.com/questions/tagged/passenger" className="btn btn-primary">Submit to Stack Overflow</a>
            </p>
          </div>
          <div className="col-sm-4">
            <h2>Enterprise support</h2>
            <p>
              If you are a
              {' '}
              <a href="https://www.phusionpassenger.com/features#premium-features">Passenger Enterprise</a>
              {' '}
              customer, then you are eligible for
              basic priority support.
            </p>
            <p>
              <small>
                For most customers, this basic priority support has a response time of 3 working days,
                with a maximum of 1 support incident per month. Please consult your contract for the
                exact support level that you are eligible for.
              </small>
            </p>
            <p>
              <a href="https://www.phusionpassenger.com/customers/help_support" className="btn btn-primary">
                Submit Enterprise support ticket
              </a>
            </p>
          </div>
          <div className="col-sm-4">
            <h2>Bug report</h2>
            <p>
              Do you suspect this error is a bug? Please send us a bug report.
            </p>
            <p>
              <em>Please attach this error page</em> so that we can help you better.
            </p>
            <p>
              <a href="http://github.com/phusion/passenger/issues" className="btn btn-primary">Submit bug report to Github</a>
            </p>
          </div>
        </div>
      </div>
    );
  }
}

export default GetHelpView;
