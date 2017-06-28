def DEFAULT_RUBY_VERSION = '2.3.4'
def LINUX_COMPILE_CONCURRENCY = 4
def LINUX_ENV = ["TEST_RUBY_VERSION=${DEFAULT_RUBY_VERSION}", "COMPILE_CONCURRENCY=${LINUX_COMPILE_CONCURRENCY}"]
def MACOS_COMPILE_CONCURRENCY = 2
def MACOS_ENV = ["TEST_RUBY_VERSION=${DEFAULT_RUBY_VERSION}", "COMPILE_CONCURRENCY=${MACOS_COMPILE_CONCURRENCY}"]

pipeline {
  agent any

  options {
    buildDiscarder(logRotator(numToKeepStr: '15'))
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
                        try {
                          sh './dev/ci/run-tests-with-docker ruby'
                        } finally {
                          sh 'mv buildout/artifacts buildout/RUBY_LINUX'
                          archiveArtifacts artifacts: 'buildout/RUBY_LINUX/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-natively ruby'
                        } finally {
                          sh 'mv buildout/artifacts buildout/RUBY_MACOS'
                          archiveArtifacts artifacts: 'buildout/RUBY_MACOS/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-with-docker nodejs'
                        } finally {
                          sh 'mv buildout/artifacts buildout/NODEJS_LINUX'
                          archiveArtifacts artifacts: 'buildout/NODEJS_LINUX/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-natively nodejs'
                        } finally {
                          sh 'mv buildout/artifacts buildout/NODEJS_MACOS'
                          archiveArtifacts artifacts: 'buildout/NODEJS_MACOS/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-with-docker cxx'
                        } finally {
                          sh 'mv buildout/artifacts buildout/CXX_LINUX'
                          archiveArtifacts artifacts: 'buildout/CXX_LINUX/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-with-docker cxx'
                        } finally {
                          sh 'mv buildout/artifacts buildout/CXX_LINUX_ROOT'
                          archiveArtifacts artifacts: 'buildout/CXX_LINUX_ROOT/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-natively cxx'
                        } finally {
                          sh 'mv buildout/artifacts buildout/CXX_MACOS'
                          archiveArtifacts artifacts: 'buildout/CXX_MACOS/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-with-docker apache2'
                        } finally {
                          sh 'mv buildout/artifacts buildout/APACHE2_LINUX'
                          archiveArtifacts artifacts: 'buildout/APACHE2_LINUX/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-natively apache2'
                        } finally {
                          sh 'mv buildout/artifacts buildout/APACHE2_MACOS'
                          archiveArtifacts artifacts: 'buildout/APACHE2_MACOS/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-with-docker nginx'
                        } finally {
                          sh 'mv buildout/artifacts buildout/NGINX_LINUX'
                          archiveArtifacts artifacts: 'buildout/NGINX_LINUX/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-natively nginx'
                        } finally {
                          sh 'mv buildout/artifacts buildout/NGINX_MACOS'
                          archiveArtifacts artifacts: 'buildout/NGINX_MACOS/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-with-docker nginx-dynamic'
                        } finally {
                          sh 'mv buildout/artifacts buildout/NGINX_DYNAMIC_LINUX'
                          archiveArtifacts artifacts: 'buildout/NGINX_DYNAMIC_LINUX/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-natively nginx-dynamic'
                        } finally {
                          sh 'mv buildout/artifacts buildout/NGINX_DYNAMIC_MACOS'
                          archiveArtifacts artifacts: 'buildout/NGINX_DYNAMIC_MACOS/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-with-docker standalone'
                        } finally {
                          sh 'mv buildout/artifacts buildout/STANDALONE_LINUX'
                          archiveArtifacts artifacts: 'buildout/STANDALONE_LINUX/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-natively standalone'
                        } finally {
                          sh 'mv buildout/artifacts buildout/STANDALONE_MACOS'
                          archiveArtifacts artifacts: 'buildout/STANDALONE_MACOS/**'
                        }
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
                        try {
                          sh './dev/ci/run-tests-with-docker source-packaging'
                        } finally {
                          sh 'mv buildout/artifacts buildout/SOURCE_PACKAGING'
                          archiveArtifacts artifacts: 'buildout/SOURCE_PACKAGING/**'
                        }
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
