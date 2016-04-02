.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

Policy TODO List
================

# Ruleset schema

- Should relesets be able to share definitions? The rulesets are self contained. Rulesets uniquely define policy for one application. Redefinition of a ruleset will have no effect on other applications. The implication is that if one ruleset has a large definition of users, user groups, or policy settings then these definitions can not be shared with other rulesets and *they must be repeated*.

- Are the policy settings adequate? Do they provide enough controls over AMQP?

    - The policy can allow or deny access to source/target **$management**. But once that access is granted the user can do anything to any object on any router. Finer control over management is desirable.

    - Are user connection counts specified at the proper level? They are specified per application but the counts are applied equally to users in all user groups.

- Are white list rules (only allow rules) enough? Having only white list rules makes rulesets (1) easier to comprehend and (2) easier to generate. The code to process them is also (3) easier to write and (4) easier to test. Mixed allow and deny rules provide a richer rule specification but with a cost impacting each of the benefit points.

- Policy adds map schema objects. The schema processor verifies that the objects are maps but does not look into the map to see if it makes any sense for policy. Policy code thoroughly validates the maps but it would be better if the maps were specified in the management schema and verified there.

    - Schema map objects forced modifying the config processor to allow .json files in addition to the existing .conf files.

# Policy service

- Current policy is local only. Certainly policy rulesets are manageable entities and may be loaded through normal management channels. There is an opportunity to develop a policy service where routers could open a channel to **$policy** for user lookup and policy settings. Then each router would not need to maintain local policy in its current form.

# Implementation

- Effecting denials of application connections, sessions, and links are all handled in the same way. Take an Open for instance. If an Open is denied then QDR returns an Open to the client followed immediately with a Close that has a condition and a description. In all cases the condition is amqp:resource-limit-exceeded. An improvement would be to use amqp:unauthorized-access for denials that are hard policy denials and use resource-limit-exceeded for connections denied by connection counts that may change later. 

- Locking. Needs review.

- Memory pool for policy settings. Uses alloc/free for each connection for small setting structs. Low priority.

- Add default 'default settings'. In the absence of policy settings, or in the presence of policies without some settings, dispatch router needs to insert values into AMQP fields. The current implementation hard codes these but they should all be configurable.

# Usual suspects

- Improve documentation.

- Improve self tests.
