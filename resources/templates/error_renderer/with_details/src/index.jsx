/** @jsx h */
import { render, h } from 'preact';
import PageMain from './PageMain.jsx';
import './bootstrap/bootstrap.css';
import './PageMain.css';

window.errorPageExtensions = [];

function initialize(pageMain) {
  window.ErrorPage = pageMain;
  for (var i = 0; i < window.errorPageExtensions.length; i++) {
    window.errorPageExtensions[i]();
  }
}

window.renderErrorPage = function() {
  render(
    <PageMain spec={window.spec} ref={ initialize } />,
    document.getElementById('root')
  );
}
