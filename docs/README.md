<!--
Licensed to the Apache Software Foundation (ASF) under one
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
under the License
-->

# Qpid Dispatch documentation

## Building the documentation

Documentation is built when you run `make docs`.  You need the
following tools to build the documentation.

* asciidoctor (1.5.6) for books
* asciidoc (8.6.8) for man pages

The versions above are known to work, earlier versions may or may not.

## Writing documentation

Documentation is written in AsciiDoc markup.

* 'books/': AsciiDoc source for the user guide
* 'man/': AsciiDoc source for Unix man pages
* 'notes/': Developer notes: project information, design notes, or
  anything else that's primarily of developer interest; these are not
  installed.
