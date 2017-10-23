#!/usr/bin/env python
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
# under the License.
#

import json

class Schema(object):
    schema = {}

    @staticmethod
    def i(entity, attribute):
        return Schema.schema[entity]["attributeNames"].index(attribute)

    @staticmethod
    def type(entity):
        return Schema.schema[entity]["fullyQualifiedType"]

    @staticmethod
    def attrs(entity):
        return Schema.schema[entity]['attributeNames']

    @staticmethod
    def default(entity, attribute):
        return Schema.schema[entity]["defaults"].get(attribute)

    @staticmethod
    def init(path=''):
        with open(path+"schema.json") as fp:
            data = json.load(fp)
            for entity in data["entityTypes"]:
                Schema.schema[entity] = {"attributeNames": [],
                                         "fullyQualifiedType": data["entityTypes"][entity]["fullyQualifiedType"],
                                         "defaults": {}}
                for attribute in data["entityTypes"][entity]["attributes"]:
                    Schema.schema[entity]["attributeNames"].append(attribute)
                    if "default" in data["entityTypes"][entity]["attributes"][attribute]:
                        Schema.schema[entity]["defaults"][attribute] = data["entityTypes"][entity]["attributes"][attribute]["default"]
                Schema.schema[entity]["attributeNames"].append("type")
