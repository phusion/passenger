def DEFAULT_RUBY_VERSION = '2.3.4'
def LINUX_COMPILE_CONCURRENCY = 4
def LINUX_ENV = ["TEST_RUBY_VERSION=${DEFAULT_RUBY_VERSION}", "COMPILE_CONCURRENCY=${LINUX_COMPILE_CONCURRENCY}"]
def MACOS_COMPILE_CONCURRENCY = 2
def MACOS_ENV = ["TEST_RUBY_VERSION=${DEFAULT_RUBY_VERSION}", "COMPILE_CONCURRENCY=${MACOS_COMPILE_CONCURRENCY}"]

pipeline {
  agent any

  options {
    timeout(time: 30, unit: 'MINUTES')
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
              if (params.RUBY_LINUX) {
                node('linux') {
                  timestamps() {
                    withEnv(LINUX_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host'
                        sh './dev/ci/run-tests-with-docker ruby'
                      }
                    }
                  }
                }
              } else {
                echo 'Test skipped.'
              }
            },
            'Ruby unit tests on macOS': {
              if (params.RUBY_MACOS) {
                node('macos') {
                  timestamps() {
                    withEnv(MACOS_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host ruby'
                        sh './dev/ci/run-tests-natively ruby'
                      }
                    }
                  }
                }
              } else {
                echo 'Test skipped.'
              }
            },

            'Node.js unit tests on Linux': {
              node('linux') {
                if (params.NODEJS_LINUX) {
                  timestamps() {
                    withEnv(LINUX_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host'
                        sh './dev/ci/run-tests-with-docker nodejs'
                      }
                    }
                  }
                } else {
                  echo 'Test skipped.'
                }
              }
            },
            'Node.js unit tests on macOS': {
              node('macos') {
                if (params.NODEJS_MACOS) {
                  timestamps() {
                    withEnv(MACOS_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host nodejs'
                        sh './dev/ci/run-tests-natively nodejs'
                      }
                    }
                  }
                } else {
                  echo 'Test skipped.'
                }
              }
            },

            'C++ unit tests on Linux, normal user': {
              node('linux') {
                if (params.CXX_LINUX) {
                  timestamps() {
                    withEnv(LINUX_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host'
                        sh './dev/ci/run-tests-with-docker cxx'
                      }
                    }
                  }
                } else {
                  echo 'Test skipped.'
                }
              }
            },
            'C++ unit tests on Linux, as root': {
              node('linux') {
                if (params.CXX_LINUX_ROOT) {
                  timestamps() {
                    withEnv(LINUX_ENV + ['SUDO=1']) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host'
                        sh './dev/ci/run-tests-with-docker cxx'
                      }
                    }
                  }
                } else {
                  echo 'Test skipped.'
                }
              }
            },
            'C++ unit tests on macOS': {
              node('macos') {
                if (params.CXX_MACOS) {
                  timestamps() {
                    withEnv(MACOS_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host cxx'
                        sh './dev/ci/run-tests-natively cxx'
                      }
                    }
                  }
                } else {
                  echo 'Test skipped.'
                }
              }
            },

            'Apache integration tests on Linux': {
              node('linux') {
                if (params.APACHE2_LINUX) {
                  timestamps() {
                    withEnv(LINUX_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host'
                        sh './dev/ci/run-tests-with-docker apache2'
                      }
                    }
                  }
                } else {
                  echo 'Test skipped.'
                }
              }
            },
            'Apache integration tests on macOS': {
              node('macos') {
                if (params.APACHE2_MACOS) {
                  timestamps() {
                    withEnv(MACOS_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host apache2'
                        sh './dev/ci/run-tests-natively apache2'
                      }
                    }
                  }
                } else {
                  echo 'Test skipped.'
                }
              }
            },

            'Nginx integration tests on Linux': {
              node('linux') {
                if (params.NGINX_LINUX) {
                  timestamps() {
                    withEnv(LINUX_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host'
                        sh './dev/ci/run-tests-with-docker nginx'
                      }
                    }
                  }
                } else {
                  echo 'Test skipped.'
                }
              }
            },
            'Nginx integration tests on macOS': {
              node('macos') {
                if (params.NGINX_MACOS) {
                  timestamps() {
                    withEnv(MACOS_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host nginx'
                        sh './dev/ci/run-tests-natively nginx'
                      }
                    }
                  }
                } else {
                  echo 'Test skipped.'
                }
              }
            },

            'Nginx dynamic module compatibility test on Linux': {
              node('linux') {
                if (params.NGINX_DYNAMIC_LINUX) {
                  timestamps() {
                    withEnv(LINUX_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host'
                        sh './dev/ci/run-tests-with-docker nginx-dynamic'
                      }
                    }
                  }
                } else {
                  echo 'Test skipped.'
                }
              }
            },
            'Nginx dynamic module compatibility test on macOS': {
              node('macos') {
                if (params.NGINX_DYNAMIC_MACOS) {
                  timestamps() {
                    withEnv(MACOS_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host nginx-dynamic'
                        sh './dev/ci/run-tests-natively nginx-dynamic'
                      }
                    }
                  }
                } else {
                  echo 'Test skipped.'
                }
              }
            },

            'Passenger Standalone integration tests on Linux': {
              node('linux') {
                if (params.STANDALONE_LINUX) {
                  timestamps() {
                    withEnv(LINUX_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host'
                        sh './dev/ci/run-tests-with-docker standalone'
                      }
                    }
                  }
                } else {
                  echo 'Test skipped.'
                }
              }
            },
            'Passenger Standalone integration tests on macOS': {
              node('macos') {
                if (params.STANDALONE_MACOS) {
                  timestamps() {
                    withEnv(MACOS_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host standalone'
                        sh './dev/ci/run-tests-natively standalone'
                      }
                    }
                  }
                } else {
                  echo 'Test skipped.'
                }
              }
            },

            'Source packaging unit tests': {
              node('') {
                if (params.SOURCE_PACKAGING) {
                  timestamps() {
                    withEnv(LINUX_ENV) {
                      checkout scm
                      ansiColor('xterm') {
                        sh './dev/ci/setup-host'
                        sh './dev/ci/run-tests-with-docker source-packaging'
                      }
                    }
                  }
                } else {
                  echo 'Test skipped.'
                }
              }
            }
          )
        }
      }
    }
  }
}
