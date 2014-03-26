##
## Licensed to the Apache Software Foundation (ASF) under one
## or more contributor license agreements.  See the NOTICE file
## distributed with this work for additional information
## regarding copyright ownership.  The ASF licenses this file
## to you under the Apache License, Version 2.0 (the
## "License"); you may not use this file except in compliance
## with the License.  You may obtain a copy of the License at
##
##   http://www.apache.org/licenses/LICENSE-2.0
##
## Unless required by applicable law or agreed to in writing,
## software distributed under the License is distributed on an
## "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
## KIND, either express or implied.  See the License for the
## specific language governing permissions and limitations
## under the License
##

#
# config_schema =
#    { <section_name> :
#        (<singleton>,
#         {<key> : (<value-type>, <index>, <flags>, <default-value>, <choices>)
#        )
#    }
#
#  <section-name>  = String name of a configuration section
#  <singleton>     = False => There may be 0 or more sections with this name
#                    True  => There must be exactly one section with this name
#  <key>           = String key of a section's key-value pair
#  <value-type>    = Python type for the value
#  <index>         = None => This value is not an index for multiple sections
#                    >= 0 => Ordinal of this value in the section primary-key
#  <flags>         = Set of characters:
#                    M = Mandatory (no default value)
#                    E = Expand referenced section into this record
#                    S = During expansion, this key should be copied
#  <default-value> = If not mandatory and not specified, the value defaults to this
#                    value
#  <choices>       = If the value is enumerated, this is a list of valid enumerations.
#

config_schema = {
    'container' : (True, {
        'worker-threads' : (int, None, '', 1,    None),
        'container-name' : (str, None, '', None, None)
    }),
    'ssl-profile' : (False, {
        'name'          : (str, 0,    'M', None, None),
        'cert-db'       : (str, None, 'S', None, None),
        'cert-file'     : (str, None, 'S', None, None),
        'key-file'      : (str, None, 'S', None, None),
        'password-file' : (str, None, 'S', None, None),
        'password'      : (str, None, 'S', None, None)
    }),
    'listener' : (False, {
        'addr'              : (str,  0,    'M', None,  None),
        'port'              : (str,  1,    'M', None,  None),
        'label'             : (str,  None, '',  None,  None),
        'role'              : (str,  None, '',  'normal', ['normal', 'inter-router']),
        'sasl-mechanisms'   : (str,  None, 'M', None,  None),
        'ssl-profile'       : (str,  None, 'E', None,  None),
        'require-peer-auth' : (bool, None, '',  True,  None),
        'trusted-certs'     : (str,  None, '',  None,  None),
        'allow-unsecured'   : (bool, None, '',  False, None),
        'max-frame-size'    : (int,  None, '',  65536, None)
    }),
    'connector' : (False, {
        'addr'            : (str,  0,    'M', None,  None),
        'port'            : (str,  1,    'M', None,  None),
        'label'           : (str,  None, '',  None,  None),
        'role'            : (str,  None, '',  'normal', ['normal', 'inter-router']),
        'sasl-mechanisms' : (str,  None, 'M', None,  None),
        'ssl-profile'     : (str,  None, 'E', None,  None),
        'allow-redirect'  : (bool, None, '',  True,  None),
        'max-frame-size'  : (int,  None, '',  65536, None)
    }),
    'router' : (True, {
        'mode'                : (str, None, '', 'standalone', ['standalone', 'interior']),
        'router-id'           : (str, None, 'M', None, None),
        'area'                : (str, None, '',  None, None),
        'hello-interval'      : (int, None, '',  1, None),
        'hello-max-age'       : (int, None, '',  3, None),
        'ra-interval'         : (int, None, '',  30, None),
        'remote-ls-max-age'   : (int, None, '',  60, None),
        'mobile-addr-max-age' : (int, None, '',  60, None)
    })
}
