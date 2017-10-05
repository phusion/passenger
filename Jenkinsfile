def DEFAULT_RUBY_VERSION = '2.3.4'
def LINUX_COMPILE_CONCURRENCY = 4
def LINUX_ENV = ["TEST_RUBY_VERSION=${DEFAULT_RUBY_VERSION}", "COMPILE_CONCURRENCY=${LINUX_COMPILE_CONCURRENCY}"]
def MACOS_COMPILE_CONCURRENCY = 2
def MACOS_ENV = ["TEST_RUBY_VERSION=${DEFAULT_RUBY_VERSION}", "COMPILE_CONCURRENCY=${MACOS_COMPILE_CONCURRENCY}"]

def setupTest(enablerFlag, nodeLabel, environment, block) {
  if (enablerFlag) {
    node(nodeLabel) {
      withEnv(environment) {
        block()
      }
    }
  } else {
    echo 'Test skipped.'
  }
}

pipeline {
  agent any

  options {
    buildDiscarder(logRotator(numToKeepStr: '15'))
    timeout(time: 30, unit: 'MINUTES')
    timestamps()
    ansiColor('xterm')
  }

  parameters {
    // In alphabetical order so that the order matches
    // how it is displayed in Blue Ocean.
    booleanParam(name: 'APACHE2_LINUX', defaultValue: true, description: 'Apache 2 integration tests on Linux')
    booleanParam(name: 'APACHE2_MACOS', defaultValue: true, description: 'Apache 2 integration tests on macOS')
    booleanParam(name: 'CXX_LINUX_ROOT', defaultValue: true, description: 'C++ unit tests on Linux, as root')
    booleanParam(name: 'CXX_LINUX', defaultValue: true, description: 'C++ unit tests on Linux, normal user')
    booleanParam(name: 'CXX_MACOS', defaultValue: true, description: 'C++ unit tests on macOS')
    booleanParam(name: 'NGINX_DYNAMIC_LINUX', defaultValue: true, description: 'Nginx dynamic module tests on Linux')
    booleanParam(name: 'NGINX_DYNAMIC_MACOS', defaultValue: true, description: 'Nginx dynamic module tests on macOS')
    booleanParam(name: 'NGINX_LINUX', defaultValue: true, description: 'Nginx integration tests on Linux')
    booleanParam(name: 'NGINX_MACOS', defaultValue: true, description: 'Nginx integration tests on macOS')
    booleanParam(name: 'NODEJS_LINUX', defaultValue: true, description: 'Node.js unit tests on Linux')
    booleanParam(name: 'NODEJS_MACOS', defaultValue: true, description: 'Node.js unit tests on macOS')
    booleanParam(name: 'STANDALONE_LINUX', defaultValue: true, description: 'Passenger Standalone integration tests on Linux')
    booleanParam(name: 'STANDALONE_MACOS', defaultValue: true, description: 'Passenger Standalone integration tests on macOS')
    booleanParam(name: 'RUBY_LINUX', defaultValue: true, description: 'Ruby unit tests on Linux')
    booleanParam(name: 'RUBY_MACOS', defaultValue: true, description: 'Ruby unit tests on macOS')
    booleanParam(name: 'SOURCE_PACKAGING', defaultValue: true, description: 'Source packaging unit tests')
  }

  stages {
    stage('Test') {
      steps {
        script {
          parallel(
            'Ruby unit tests on Linux': {
              setupTest(params.RUBY_LINUX, 'linux', LINUX_ENV) {
                checkout scm
                sh './dev/ci/setup-host'
                sh './dev/ci/run-tests-with-docker ruby'
              }
            },
            'Ruby unit tests on macOS': {
              setupTest(params.RUBY_MACOS, 'macos', MACOS_ENV) {
                checkout scm
                sh './dev/ci/setup-host ruby'
                sh './dev/ci/run-tests-natively ruby'
              }
            },

            'Node.js unit tests on Linux': {
              setupTest(params.NODEJS_LINUX, 'linux', LINUX_ENV) {
                checkout scm
                sh './dev/ci/setup-host'
                sh './dev/ci/run-tests-with-docker nodejs'
              }
            },
            'Node.js unit tests on macOS': {
              setupTest(params.NODEJS_MACOS, 'macos', MACOS_ENV) {
                checkout scm
                sh './dev/ci/setup-host nodejs'
                sh './dev/ci/run-tests-natively nodejs'
              }
            },

            'C++ unit tests on Linux, normal user': {
              setupTest(params.CXX_LINUX, 'linux', LINUX_ENV) {
                checkout scm
                sh './dev/ci/setup-host'
                sh './dev/ci/run-tests-with-docker cxx'
              }
            },
            'C++ unit tests on Linux, as root': {
              setupTest(params.CXX_LINUX_ROOT, 'linux', LINUX_ENV + ['SUDO=1']) {
                checkout scm
                sh './dev/ci/setup-host'
                sh './dev/ci/run-tests-with-docker cxx'
              }
            },
            'C++ unit tests on macOS': {
              setupTest(params.CXX_MACOS, 'macos', MACOS_ENV) {
                checkout scm
                sh './dev/ci/setup-host cxx'
                sh './dev/ci/run-tests-natively cxx'
              }
            },

            'Apache integration tests on Linux': {
              setupTest(params.APACHE2_LINUX, 'linux', LINUX_ENV) {
                checkout scm
                sh './dev/ci/setup-host'
                sh './dev/ci/run-tests-with-docker apache2'
              }
            },
            'Apache integration tests on macOS': {
              setupTest(params.APACHE2_MACOS, 'macos', MACOS_ENV) {
                checkout scm
                sh './dev/ci/setup-host apache2'
                sh './dev/ci/run-tests-natively apache2'
              }
            },

            'Nginx integration tests on Linux': {
              setupTest(params.NGINX_LINUX, 'linux', LINUX_ENV) {
                checkout scm
                sh './dev/ci/setup-host'
                sh './dev/ci/run-tests-with-docker nginx'
              }
            },
            'Nginx integration tests on macOS': {
              setupTest(params.NGINX_MACOS, 'macos', MACOS_ENV) {
                checkout scm
                sh './dev/ci/setup-host nginx'
                sh './dev/ci/run-tests-natively nginx'
              }
            },

            'Nginx dynamic module compatibility test on Linux': {
              setupTest(params.NGINX_DYNAMIC_LINUX, 'linux', LINUX_ENV) {
                checkout scm
                sh './dev/ci/setup-host'
                sh './dev/ci/run-tests-with-docker nginx-dynamic'
              }
            },
            'Nginx dynamic module compatibility test on macOS': {
              setupTest(params.NGINX_DYNAMIC_MACOS, 'macos', MACOS_ENV) {
                checkout scm
                sh './dev/ci/setup-host nginx-dynamic'
                sh './dev/ci/run-tests-natively nginx-dynamic'
              }
            },

            'Passenger Standalone integration tests on Linux': {
              setupTest(params.STANDALONE_LINUX, 'linux', LINUX_ENV) {
                checkout scm
                sh './dev/ci/setup-host'
                sh './dev/ci/run-tests-with-docker standalone'
              }
            },
            'Passenger Standalone integration tests on macOS': {
              setupTest(params.STANDALONE_MACOS, 'macos', MACOS_ENV) {
                checkout scm
                sh './dev/ci/setup-host standalone'
                sh './dev/ci/run-tests-natively standalone'
              }
            },

            'Source packaging unit tests': {
              setupTest(params.SOURCE_PACKAGING, 'linux', LINUX_ENV) {
                checkout scm
                sh './dev/ci/setup-host'
                sh './dev/ci/run-tests-with-docker source-packaging'
              }
            }
          )
        }
      }
    }
  }
}
