#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (C) 2010-2013  Phusion
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; version 2 of the License.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

RPM_NAME = "passenger"
RPMBUILD_ROOT = File.expand_path("~/rpmbuild")
MOCK_OFFLINE = boolean_option('MOCK_OFFLINE', false)
ALL_RPM_DISTROS = {
	"el6" => { :mock_chroot_name => "epel-6", :distro_name => "Enterprise Linux 6" },
	"amazon" => { :mock_chroot_name => "epel-6", :distro_name => "Amazon Linux" }
}

desc "Build gem for use in RPM building"
task 'rpm:gem' do
	rpm_source_dir = "#{RPMBUILD_ROOT}/SOURCES"
	sh "gem build #{PACKAGE_NAME}.gemspec"
	sh "cp #{PACKAGE_NAME}-#{PACKAGE_VERSION}.gem #{rpm_source_dir}/"
end

desc "Build RPM for local machine"
task 'rpm:local' => 'rpm:gem' do
	distro_id = `./rpm/get_distro_id.py`.strip
	rpm_spec_dir = "#{RPMBUILD_ROOT}/SPECS"
	spec_target_dir = "#{rpm_spec_dir}/#{distro_id}"
	spec_target_file = "#{spec_target_dir}/#{RPM_NAME}.spec"

	sh "mkdir -p #{spec_target_dir}"
	puts "Generating #{spec_target_file}"
	Preprocessor.new.start("rpm/#{RPM_NAME}.spec.template",
		spec_target_file,
		:distribution => distro_id)

	sh "rpmbuild -ba #{spec_target_file}"
end

def create_rpm_build_task(distro_id, mock_chroot_name, distro_name)
	desc "Build RPM for #{distro_name}"
	task "rpm:#{distro_id}" => 'rpm:gem' do
		rpm_spec_dir = "#{RPMBUILD_ROOT}/SPECS"
		spec_target_dir = "#{rpm_spec_dir}/#{distro_id}"
		spec_target_file = "#{spec_target_dir}/#{RPM_NAME}.spec"
		maybe_offline = MOCK_OFFLINE ? "--offline" : nil

		sh "mkdir -p #{spec_target_dir}"
		puts "Generating #{spec_target_file}"
		Preprocessor.new.start("rpm/#{RPM_NAME}.spec.template",
			spec_target_file,
			:distribution => distro_id)

		sh "rpmbuild -bs #{spec_target_file}"
		sh "mock --verbose #{maybe_offline} " +
			"-r #{mock_chroot_name}-x86_64 " +
			"--resultdir '#{PKG_DIR}/#{distro_id}' " +
			"rebuild #{RPMBUILD_ROOT}/SRPMS/#{RPM_NAME}-#{PACKAGE_VERSION}-1#{distro_id}.src.rpm"
	end
end

ALL_RPM_DISTROS.each_pair do |distro_id, info|
	create_rpm_build_task(distro_id, info[:mock_chroot_name], info[:distro_name])
end

desc "Build RPMs for all distributions"
task "rpm:all" => ALL_RPM_DISTROS.keys.map { |x| "rpm:#{x}" }

desc "Publish RPMs for all distributions"
task "rpm:publish" do
	server = "juvia-helper.phusion.nl"
	remote_dir = "/srv/oss_binaries_passenger/yumgems/phusion-misc"
	rsync = "rsync -z -r --delete --progress"

	ALL_RPM_DISTROS.each_key do |distro_id|
		if !File.exist?("#{PKG_DIR}/#{distro_id}")
			abort "No packages built for #{distro_id}. Please run 'rake rpm:all' first."
		end
	end
	ALL_RPM_DISTROS.each_key do |distro_id|
		sh "rpm --resign --define '%_signature gpg' --define '%_gpg_name #{PACKAGE_SIGNING_KEY}' #{PKG_DIR}/#{distro_id}/*.rpm"
	end
	sh "#{rsync} #{server}:#{remote_dir}/latest/ #{PKG_DIR}/yumgems/"
	ALL_RPM_DISTROS.each_key do |distro_id|
		distro_dir = "#{PKG_DIR}/#{distro_id}"
		repo_dir = "#{PKG_DIR}/yumgems/#{distro_id}"
		sh "mkdir -p #{repo_dir}"
		sh "cp #{distro_dir}/#{RPM_NAME}*.rpm #{repo_dir}/"
		sh "createrepo #{repo_dir}"
	end
	sh "ssh #{server} 'rm -rf #{remote_dir}/new && cp -dpR #{remote_dir}/latest #{remote_dir}/new'"
	sh "#{rsync} #{PKG_DIR}/yumgems/ #{server}:#{remote_dir}/new/"
	sh "ssh #{server} 'rm -rf #{remote_dir}/previous && mv #{remote_dir}/latest #{remote_dir}/previous && mv #{remote_dir}/new #{remote_dir}/latest'"
end
