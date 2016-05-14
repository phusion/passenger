/** @jsx h */
import { Component, h } from 'preact';

class Tabs extends Component {
  constructor(props) {
    super(props);
    this.state = { activeKey: props.defaultActiveKey };
  }

  render() {
    var navs = [];
    var contents = [];
    var i;

    for (i = 0; i < this.props.children.length; i++) {
      var child = this.props.children[i];
      if (child !== undefined) {
        navs.push(this._buildNav(child));
        contents.push(this._buildContent(child));
      }
    }

    return (
      <div className={this.props.className}>
        <ul className="nav nav-tabs" role="tablist">
          {navs}
        </ul>
        <div className="tab-content">
          {contents}
        </div>
      </div>
    );
  }

  getActiveKey() {
    return this.state.activeKey;
  }

  setActiveKey(eventKey) {
    this.setState({ activeKey: eventKey });
  }

  _buildNav(tab) {
    var className;
    if (this.getActiveKey() === tab.attributes.eventKey) {
      className = 'active';
    }

    var self = this;
    var clickHandler = function(e) {
      self._handleTabClick(e, tab);
    };

    return (
      <li
      role="presentation"
      className={className}
      key={tab.attributes.eventKey}>
        <a
        href={'#' + this._buildIdForTab(tab)}
        aria-controls={this._buildIdForTab(tab)}
        role="tab"
        data-toggle="tab"
        onClick={clickHandler}>
          {tab.attributes.title}
        </a>
      </li>
    );
  }

  _buildContent(tab) {
    var className = 'tab-pane';
    if (this.getActiveKey() === tab.attributes.eventKey) {
      className += ' active';
    }

    return (
      <div
      role="tabpanel"
      className={className}
      id={this._buildIdForTab(tab)}
      key={tab.attributes.eventKey}>
        {tab}
      </div>
    );
  }

  _buildIdForTab(tab) {
    if (tab.attributes.id) {
      return tab.attributes.id;
    } else if (this.props.id === undefined || this.props.id === '') {
      return tab.attributes.eventKey;
    } else {
      return this.props.id + '-' + tab.attributes.eventKey;
    }
  }

  _handleTabClick(e, tab) {
    e.preventDefault();
    this.setActiveKey(tab.attributes.eventKey);
  }
}

export default Tabs;
