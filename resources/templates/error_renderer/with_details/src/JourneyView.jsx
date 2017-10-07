/** @jsx h */
import { Component, h } from 'preact';
import './JourneyView.css';

class JourneyView extends Component {
  render() {
    const journey = this.props.spec.journey;
    if (journey.type === 'SPAWN_DIRECTLY') {
      return this.renderSpawnDirectlyJourney();
    } else if (journey.type === 'START_PRELOADER') {
      return this.renderStartPreloaderJourney();
    } else {
      return this.renderSpawnThroughPreloaderJourney();
    }
  }

  renderSpawnDirectlyJourney() {
    const self = this;

    function renderWrapperRows() {
      if (!self.props.spec.journey.steps.SUBPROCESS_EXEC_WRAPPER) {
        return [];
      }

      if (self.props.spec.config.wrapper_supplied_by_third_party) {
        return [
          (
            <tr key="exec-wrapper">
              <td className="server-core"></td>
              <td className="process-boundary"></td>
              {self.renderCell('subprocess', 'Initialize third-party wrapper',
                ['SUBPROCESS_EXEC_WRAPPER', 'SUBPROCESS_WRAPPER_PREPARATION'])}
            </tr>
          ),
          (
            <tr server="sep1">
              <td className="server-core"></td>
              <td className="process-boundary"></td>
              {self.renderStepSeparator('subprocess')}
            </tr>
          )
        ];
      } else {
        return [
          (
            <tr key="exec-wrapper">
              <td className="server-core"></td>
              <td className="process-boundary"></td>
              {self.renderCell('subprocess', 'Initialize language runtime',
                ['SUBPROCESS_EXEC_WRAPPER', 'SUBPROCESS_WRAPPER_PREPARATION'])}
            </tr>
          ),
          (
            <tr key="sep2">
              <td className="server-core"></td>
              <td className="process-boundary"></td>
              {self.renderStepSeparator('subprocess')}
            </tr>
          )
        ];
      }
    }

    return (
      <table className="journey spawn-directly">
        <thead>
          <tr>
            <th className="server-core">
              In {this.props.spec.short_program_name} core
              <br />
              <small>PID {this.props.spec.diagnostics.core_process.pid}</small>
            </th>
            <th></th>
            <th className="subprocess">
              In subprocess
              <br />
              <small>PID {this.props.spec.diagnostics.subprocess.pid || 'unknown'}</small>
            </th>
          </tr>
        </thead>
        <tbody>
          <tr>
            {this.renderCell('server-core', 'Preparation work',
              ['SPAWNING_KIT_PREPARATION'])}
          </tr>
          <tr>
            {this.renderStepSeparator('server-core')}
          </tr>
          <tr>
            {this.renderCell('server-core', 'Spawn subprocess (fork())',
              ['SPAWNING_KIT_FORK_SUBPROCESS'])}
            {this.renderProcessBoundaryArrow()}
            {this.renderCell('subprocess', 'Basic initialization before exec()',
              ['SUBPROCESS_BEFORE_FIRST_EXEC'])}
          </tr>
          <tr>
            {this.renderStepSeparator('server-core')}
            <td className="process-boundary"></td>
            {this.renderStepSeparator('subprocess')}
          </tr>
          <tr>
            {this.renderCell('server-core', 'Handshake with subprocess',
              ['SPAWNING_KIT_HANDSHAKE_PERFORM'])}
            <td className="process-boundary"></td>
            {this.renderCell('subprocess', 'Setup environment (1)',
              ['SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL'])}
          </tr>
          <tr>
            {this.renderStepSeparator('server-core')}
            <td className="process-boundary"></td>
            {this.renderStepSeparator('subprocess')}
          </tr>
          <tr>
            {this.renderCell('server-core', 'Finish',
              ['SPAWNING_KIT_FINISH'])}
            <td className="process-boundary"></td>
            {this.renderCell('subprocess', 'Load OS shell',
              ['SUBPROCESS_OS_SHELL'])}
          </tr>
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderStepSeparator('subprocess')}
          </tr>
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderCell('subprocess', 'Setup environment (2)',
              ['SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL'])}
          </tr>
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderStepSeparator('subprocess')}
          </tr>
          {renderWrapperRows()}
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderCell('subprocess', 'Load or execute application',
              ['SUBPROCESS_APP_LOAD_OR_EXEC'])}
          </tr>
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderStepSeparator('subprocess')}
          </tr>
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderCell('subprocess', 'App start listening for requests',
              ['SUBPROCESS_LISTEN'])}
          </tr>
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderStepSeparator('subprocess')}
          </tr>
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderCell('subprocess', 'Finish',
              ['SUBPROCESS_FINISH'])}
          </tr>
        </tbody>
      </table>
    );
  }

  renderStartPreloaderJourney() {
    const self = this;

    function renderWrapperRows() {
      if (!self.props.spec.journey.steps.SUBPROCESS_EXEC_WRAPPER) {
        return [];
      }

      if (self.props.spec.config.wrapper_supplied_by_third_party) {
        return [
          (
            <tr key="exec-wrapper">
              <td className="server-core"></td>
              <td className="process-boundary"></td>
              {self.renderCell('subprocess', 'Initialize language runtime',
                ['SUBPROCESS_EXEC_WRAPPER'])}
            </tr>
          ),
          (
            <tr key="sep1">
              <td className="server-core"></td>
              <td className="process-boundary"></td>
              {self.renderStepSeparator('subprocess')}
            </tr>
          ),
          (
            <tr key="prep-inside-wrapper">
              <td className="server-core"></td>
              <td className="process-boundary"></td>
              {self.renderCell('subprocess', 'Initialize third-party preloading wrapper',
                ['SUBPROCESS_WRAPPER_PREPARATION'])}
            </tr>
          ),
          (
            <tr key="sep2">
              <td className="server-core"></td>
              <td className="process-boundary"></td>
              {self.renderStepSeparator('subprocess')}
            </tr>
          )
        ];
      } else {
        return [
          (
            <tr key="exec-wrapper">
              <td className="server-core"></td>
              <td className="process-boundary"></td>
              {self.renderCell('subprocess', 'Initialize language runtime',
                ['SUBPROCESS_EXEC_WRAPPER'])}
            </tr>
          ),
          (
            <tr key="sep1">
              <td className="server-core"></td>
              <td className="process-boundary"></td>
              {self.renderStepSeparator('subprocess')}
            </tr>
          ),
          (
            <tr key="prep-inside-wrapper">
              <td className="server-core"></td>
              <td className="process-boundary"></td>
              {self.renderCell('subprocess',
                'Initialize ' + self.props.spec.short_program_name + '-internal preloading wrapper',
                ['SUBPROCESS_WRAPPER_PREPARATION'])}
            </tr>
          ),
          (
            <tr key="sep2">
              <td className="server-core"></td>
              <td className="process-boundary"></td>
              {self.renderStepSeparator('subprocess')}
            </tr>
          )
        ];
      }
    }

    return (
      <table className="journey start-preloader">
        <thead>
          <tr>
            <th className="server-core">
              In {this.props.spec.short_program_name} core
              <br />
              <small>PID {this.props.spec.diagnostics.core_process.pid}</small>
            </th>
            <th></th>
            <th className="subprocess">
              In subprocess
              <br />
              <small>PID {this.props.spec.diagnostics.subprocess.pid || 'unknown'}</small>
            </th>
          </tr>
        </thead>
        <tbody>
          <tr>
            {this.renderCell('server-core', 'Preparation work',
              ['SPAWNING_KIT_PREPARATION'])}
          </tr>
          <tr>
            {this.renderStepSeparator('server-core')}
          </tr>
          <tr>
            {this.renderCell('server-core', 'Spawn subprocess (fork())',
              ['SPAWNING_KIT_FORK_SUBPROCESS'])}
            {this.renderProcessBoundaryArrow()}
            {this.renderCell('subprocess', 'Basic initialization before exec()',
              ['SUBPROCESS_BEFORE_FIRST_EXEC'])}
          </tr>
          <tr>
            {this.renderStepSeparator('server-core')}
            <td className="process-boundary"></td>
            {this.renderStepSeparator('subprocess')}
          </tr>
          <tr>
            {this.renderCell('server-core', 'Handshake with subprocess',
              ['SPAWNING_KIT_HANDSHAKE_PERFORM'])}
            <td className="process-boundary"></td>
            {this.renderCell('subprocess', 'Setup environment (1)',
              ['SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL'])}
          </tr>
          <tr>
            {this.renderStepSeparator('server-core')}
            <td className="process-boundary"></td>
            {this.renderStepSeparator('subprocess')}
          </tr>
          <tr>
            {this.renderCell('server-core', 'Finish',
              ['SPAWNING_KIT_FINISH'])}
            <td className="process-boundary"></td>
            {this.renderCell('subprocess', 'Load OS shell',
              ['SUBPROCESS_OS_SHELL'])}
          </tr>
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderStepSeparator('subprocess')}
          </tr>
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderCell('subprocess', 'Setup environment (2)',
              ['SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL'])}
          </tr>
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderStepSeparator('subprocess')}
          </tr>
          {renderWrapperRows()}
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderCell('subprocess', 'Load application',
              ['SUBPROCESS_APP_LOAD_OR_EXEC'])}
          </tr>
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderStepSeparator('subprocess')}
          </tr>
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderCell('subprocess', 'App start listening for preloader commands',
              ['SUBPROCESS_LISTEN'])}
          </tr>
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderStepSeparator('subprocess')}
          </tr>
          <tr>
            <td className="server-core"></td>
            <td className="process-boundary"></td>
            {this.renderCell('subprocess', 'Finish',
              ['SUBPROCESS_FINISH'])}
          </tr>
        </tbody>
      </table>
    );
  }

  renderSpawnThroughPreloaderJourney() {
    return (
      <table className="journey spawn-through-preloader">
        <thead>
          <tr>
            <th className="server-core">
              In {this.props.spec.short_program_name}
              <br />
              <small>PID {this.props.spec.diagnostics.core_process.pid}</small>
            </th>
            <th></th>
            <th className="preloader">
              In preloader
              <br />
              <small>PID {this.props.spec.diagnostics.preloader_process.pid || 'unknown'}</small>
            </th>
            <th></th>
            <th className="subprocess">
              In subprocess
              <br />
              <small>PID {this.props.spec.diagnostics.subprocess.pid || 'unknown'}</small>
            </th>
          </tr>
        </thead>
        <tbody>
          <tr>
            {this.renderCell('server-core', 'Preparation work',
              ['SPAWNING_KIT_PREPARATION'])}
          </tr>
          <tr>
            {this.renderStepSeparator('server-core')}
          </tr>
          <tr>
            {this.renderCell('server-core', 'Tell preloader to spawn a subprocess',
              ['SPAWNING_KIT_CONNECT_TO_PRELOADER', 'SPAWNING_KIT_SEND_COMMAND_TO_PRELOADER'])}
            {this.renderProcessBoundaryArrow('small')}
            {this.renderCell('preloader', 'Preparation work',
              ['PRELOADER_PREPARATION'])}
          </tr>
          <tr>
            {this.renderStepSeparator('server-core')}
            <td className="process-boundary"></td>
            {this.renderStepSeparator('preloader')}
          </tr>
          <tr>
            {this.renderCell('server-core', 'Receive and process preloader response',
              ['SPAWNING_KIT_READ_RESPONSE_FROM_PRELOADER',
                'SPAWNING_KIT_PARSE_RESPONSE_FROM_PRELOADER',
                'SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER'])}
            <td className="process-boundary"></td>
            {this.renderCell('preloader', 'Spawn subprocess (fork())',
              ['PRELOADER_FORK_SUBPROCESS'])}
            {this.renderProcessBoundaryArrow('small')}
            {this.renderCell('subprocess', 'Preparation',
              ['SUBPROCESS_PREPARE_AFTER_FORKING_FROM_PRELOADER'])}
          </tr>
          <tr>
            {this.renderStepSeparator('server-core')}
            <td className="process-boundary"></td>
            {this.renderStepSeparator('preloader')}
            <td className="process-boundary"></td>
            {this.renderStepSeparator('subprocess')}
          </tr>
          <tr>
            {this.renderCell('server-core', 'Handshake with subprocess',
              ['SPAWNING_KIT_HANDSHAKE_PERFORM'])}
            <td className="process-boundary"></td>
            {this.renderCell('preloader',
              'Send response to ' + this.props.spec.short_program_name + ' core',
              ['PRELOADER_SEND_RESPONSE'])}
            <td className="process-boundary"></td>
            {this.renderCell('subprocess', 'App start listening',
              ['SUBPROCESS_LISTEN'])}
          </tr>
          <tr>
            {this.renderStepSeparator('server-core')}
            <td className="process-boundary"></td>
            {this.renderStepSeparator('preloader')}
            <td className="process-boundary"></td>
            {this.renderStepSeparator('subprocess')}
          </tr>
          <tr>
            {this.renderCell('server-core', 'Finish',
              ['SPAWNING_KIT_FINISH'])}
            <td className="process-boundary"></td>
            {this.renderCell('preloader', 'Finish',
              ['PRELOADER_FINISH'])}
            <td className="process-boundary"></td>
            {this.renderCell('subprocess', 'Finish',
              ['SUBPROCESS_FINISH'])}
          </tr>
        </tbody>
      </table>
    );
  }

  renderCell(processName, title, steps) {
    const journey = this.props.spec.journey;

    function anyStepFailed() {
      var i;
      for (i = 0; i < steps.length; i++) {
        var step = steps[i];
        if (journey.steps[step].state === 'STEP_ERRORED') {
          return true;
        }
      }
      return false;
    }

    function allStepsDone() {
      var i;
      for (i = 0; i < steps.length; i++) {
        var step = steps[i];
        if (journey.steps[step].state !== 'STEP_PERFORMED') {
          return false;
        }
      }
      return true;
    }

    function allStepsNotStarted() {
      var i;
      for (i = 0; i < steps.length; i++) {
        var step = steps[i];
        if (journey.steps[step].state !== 'STEP_NOT_STARTED') {
          return false;
        }
      }
      return true;
    }

    function renderDuration() {
      var i, totalDurationSec;
      for (i = 0; i < steps.length; i++) {
        var step = steps[i];
        if (journey.steps[step].duration !== undefined) {
          if (totalDurationSec === undefined) {
            totalDurationSec = 0;
          }
          totalDurationSec += journey.steps[step].duration;
        } else if (journey.steps[step].begin_time !== undefined) {
          if (totalDurationSec === undefined) {
            totalDurationSec = 0;
          }
          // relative_timestamp is negative
          totalDurationSec -= journey.steps[step].begin_time.relative_timestamp;
        }
      }
      if (totalDurationSec !== undefined) {
        if (allStepsNotStarted()) {
          return (<span className="duration">&mdash; skipped</span>);
        } else {
          return (<span className="duration">&mdash; {totalDurationSec.toFixed(1)}s</span>);
        }
      }
    }

    var statusClass, statusLabel;
    if (anyStepFailed()) {
      statusClass = 'error';
      statusLabel = (
        <span className="glyphicon glyphicon-remove" aria-hidden="true"></span>
      );
    } else if (allStepsDone()) {
      statusClass = 'done';
      statusLabel = (
        <span className="glyphicon glyphicon-ok" aria-hidden="true"></span>
      );
    } else if (allStepsNotStarted()) {
      statusClass = 'not-started';
      statusLabel = (
        <span className="glyphicon glyphicon-unchecked" aria-hidden="true"></span>
      );
    } else {
      statusClass = 'in-progress';
      statusLabel = (
        <span className="glyphicon glyphicon-option-horizontal" aria-hidden="true"></span>
      );
    }

    var className = processName + ' ' + statusClass;

    return (
      <td className={className}>
        <span className="status-label">{statusLabel}</span>
        <span className="title">{title} {renderDuration()}</span>
      </td>
    );
  }

  renderStepSeparator(componentName) {
    return (
      <td className={ `${componentName} step-separator` }>
        |
      </td>
    );
  }

  renderProcessBoundaryArrow(size) {
    var width;
    if (size === 'small') {
      width = 90;
    } else {
      width = 130;
    }
    return (
      <td className="process-boundary arrow">
        <svg
         width={width}
         height="20"
         viewBox="0 0 130 20"
         version="1.1"
         className="arrow-image">
          <defs
             id="defs4">
            <marker
               orient="auto"
               refY="0"
               refX="0"
               id="TriangleOutM"
               style="overflow:visible">
              <path
                 id="path4287"
                 d="m 5.77,0 -8.65,5 0,-10 8.65,5 z"
                 style="fill:#aaa;fill-opacity:1;fill-rule:evenodd;stroke:#aaa;stroke-width:1pt;stroke-opacity:1"
                 transform="scale(0.4,0.4)" />
            </marker>
            <marker
               orient="auto"
               refY="0"
               refX="0"
               id="Arrow2Lend"
               style="overflow:visible">
              <path
                 id="path4163"
                 style="fill:#aaa;fill-opacity:1;fill-rule:evenodd;stroke:#aaa;stroke-width:0.625;stroke-linejoin:round;stroke-opacity:1"
                 d="M 8.7185878,4.0337352 -2.2072895,0.01601326 8.7185884,-4.0017078 c -1.7454984,2.3720609 -1.7354408,5.6174519 -6e-7,8.035443 z"
                 transform="matrix(-1.1,0,0,-1.1,-1.1,0)" />
            </marker>
            <marker
               orient="auto"
               refY="0"
               refX="0"
               id="Arrow1Lend"
               style="overflow:visible">
              <path
                 id="path4145"
                 d="M 0,0 5,-5 -12.5,0 5,5 0,0 Z"
                 style="fill:#aaa;fill-opacity:1;fill-rule:evenodd;stroke:#aaa;stroke-width:1pt;stroke-opacity:1"
                 transform="matrix(-0.8,0,0,-0.8,-10,0)" />
            </marker>
          </defs>
          <g
           id="layer1"
           transform="translate(0,-1032.3622)">
            <path
               style="fill:none;fill-rule:evenodd;stroke:#aaa;stroke-width:3.99429917;stroke-linecap:butt;stroke-linejoin:miter;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1;marker-end:url(#TriangleOutM)"
               d="m 0,1042.3622 118.75284,0"
               id="path3336" />
          </g>
        </svg>
      </td>
    )
  }
}

export default JourneyView;
