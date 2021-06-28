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

from concurrent import futures

import grpc

from friendship_pb2 import Person, CreateResult, PersonEmail, CommonFriendsResult, FriendshipResponse
from friendship_pb2_grpc import FriendshipServicer, add_FriendshipServicer_to_server


class FriendShipService(FriendshipServicer):
    """
    Implementation of the gRPC FriendshipServicer.
    See the friendship.proto definition for more info.
    """

    def __init__(self):
        self.people = list()

    def Create(self, request, context):
        person = request  # type: Person
        res = CreateResult()  # type: CreateResult
        if person.email in [p.email for p in self.people]:
            res.success = False
            res.message = "Person already exists (email: %s)" % person.email
            return res

        self.people.append(person)
        res.success = True
        return res

    def ListFriends(self, request, context):
        pe = request  # type: PersonEmail
        for p in self.people:
            if pe.email in p.email:
                for friend in p.friends:
                    yield self.get_person(friend)

    def CommonFriendsCount(self, request_iterator, context):
        res = CommonFriendsResult()
        fs = {p.email for p in self.people}
        for pe in request_iterator:
            common_friends = [p.email for p in self.people if pe.email in p.friends]
            fs.intersection_update(common_friends)
        res.friends.extend(fs)
        res.count = len(res.friends)
        return res

    def MakeFriends(self, request_iterator, context):
        for fr in request_iterator:
            res = FriendshipResponse()
            try:
                p1 = self.get_person(fr.email1)
                p2 = self.get_person(fr.email2)
                if None in [res.friend1, res.friend2]:
                    res.error = "Invalid email provided"
                else:
                    if fr.email2 not in p1.friends:
                        p1.friends.append(fr.email2)
                    if fr.email1 not in p2.friends:
                        p2.friends.append(fr.email1)
                    res.friend1.CopyFrom(p1)
                    res.friend2.CopyFrom(p2)
            except Exception as e:
                res.error = e.__str__()
            finally:
                yield res

    def get_person(self, email):
        for p in self.people:
            if p.email == email:
                return p
        return None


def serve(port, options=None):
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=1),
                         options=options)
    add_FriendshipServicer_to_server(FriendShipService(), server)
    server.add_insecure_port('[::]:%s' % port)
    server.start()
    return server
