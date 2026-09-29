# Build and test environment for the Apache module, on a distribution that
# ships a recent Apache. See README.md.
FROM rockylinux/rockylinux:10

RUN dnf -y install --setopt=install_weak_deps=False \
        apr-devel \
        apr-util-devel \
        gcc-c++ \
        git \
        httpd \
        httpd-devel \
        libcurl-devel \
        make \
        mod_ssl \
        nodejs \
        npm \
        openssl-devel \
        pcre2-devel \
        procps-ng \
        redhat-rpm-config \
        ruby \
        ruby-devel \
        rubygem-bundler \
        rubygem-rake \
        which \
 && dnf clean all

# The WebSocket demo application is not part of the bundle, so it needs Rack
# available as a system gem.
RUN gem install --no-document rack rackup

# Passenger refuses to run applications as root, and so do parts of the test
# suite. The uid is fixed rather than matched to the host user, which is fine
# on macOS because Docker Desktop squashes ownership on bind mounts; on a Linux
# host with a different uid, files this container writes into the source tree
# come out owned by uid 1000.
RUN useradd --create-home --uid 1000 app

# Gems go into a volume, so that repeated runs do not re-resolve the bundle.
ENV BUNDLE_PATH=/bundle \
    LC_ALL=C.UTF-8

WORKDIR /passenger
