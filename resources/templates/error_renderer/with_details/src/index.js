/** @jsx h */
import { render, h } from 'preact';
import PageMain from './PageMain.jsx';
import './bootstrap/bootstrap.css';
import './PageMain.css';

render(
  <PageMain spec={window.spec} />,
  document.getElementById('root')
);
