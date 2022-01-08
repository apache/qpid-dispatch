#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License
#

"""
DisplayNameService provides the mapping needed to associate an un-friendly user identifier to a more friendly
user nick name.
Maintains a dict (profile_dict) of ssl profile names to SSLProfile objects. The SSLProfile objects are built using
the file name which contains a mapping of user identifiers to user names.
"""

import json
import traceback

from qpid_dispatch_internal import dispatch


class SSLProfile:
    def __init__(self, profile_name, profile_file):
        super(SSLProfile, self).__init__()
        self.profile_name = profile_name
        self.profile_file = profile_file
        self.cache = {}
        with open(profile_file) as json_data:
            d = json.load(json_data)
            for key in d.keys():
                self.cache[key] = d[key]

    def __repr__(self):
        return "SSLProfile(%s)" % ", ".join("%s=%s" % (k, self.cache[k]) for k in self.cache.keys())


class DisplayNameService:

    def __init__(self):
        super(DisplayNameService, self).__init__()
        # profile_dict will be a mapping from ssl_profile_name to the SSLProfile object
        self.profile_dict = {}
        self.io_adapter = None
        self.log_adapter = dispatch.LogAdapter("DISPLAYNAME")

    def log(self, level, text):
        info = traceback.extract_stack(limit=2)[0]  # Caller frame info
        self.log_adapter.log(level, text, info[0], info[1])

    def add(self, profile_name, profile_file_location):
        ssl_profile = SSLProfile(profile_name, profile_file_location)
        self.profile_dict[profile_name] = ssl_profile
        self.log(dispatch.LOG_INFO, "Added profile name %s, profile file location %s to DisplayNameService" % (profile_name, profile_file_location))

    def remove(self, profile_name):
        try:
            del self.profile_dict[profile_name]
        except KeyError:
            pass

    def reload_all(self):
        for profile_name in self.profile_dict.keys():
            self.add(profile_name, self.profile_dict[profile_name].profile_file)

    def reload(self, profile_name=None):
        if profile_name:
            self.add(profile_name, self.profile_dict[profile_name].profile_file)
        else:
            self.reload_all()

    def query(self, profile_name, user_id):
        self.log(dispatch.LOG_TRACE, "Received query for profile name %s, user id %s to DisplayNameService" %
                 (profile_name, user_id))
        ssl_profile = self.profile_dict.get(profile_name)
        if ssl_profile:
            profile_cache = self.profile_dict.get(profile_name).cache
            user_name = profile_cache.get(user_id)
            return user_name if user_name else user_id
        else:
            return user_id
