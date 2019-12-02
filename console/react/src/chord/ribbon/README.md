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

  This is a replacement for the d3.svg.chord() ribbon generator.
  The native d3 implementation is efficient, but its chords can become 'twisted'
  in certain curcumatances.

  A chord has up to 4 components:
  1. A beginning arc along the edge of a circle
  2. A quadratic bezier curve across the circle to the start of another arc
  3. A 2nd arc along the edge of the circle
  4. A quadratic bezier curve to the start of the 1st arc

  Components 2 and 3 are dropped if the chord has only one endpoint.

  The problem arises when the arcs are very close to each other and one arc is significantly
  larger than the other. The inner bezier curve connecting the arcs extends towards the center
  of the circle. The outer bezier curve connecting the outer ends of the arc crosses the inner
  bezier curve causing the chords to look twisted.

  The solution implemented here is to adjust the inner bezier curve to not extend into the circle very far.
  That is done by changing its control point. Instead of the control point being at the center
  of the circle, it is moved towards the edge of the circle in the direction of the midpoint of 
  the bezier curve's end points.
