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

  This is a replacement for d3.layout.chord().
  It implements a groubBy feature that allows arcs to be grouped.
  It does not implement the sortBy features of d3.layout.chord.

  API: 
  qrdlayoutChord().padding(ARCPADDING).groupBy(groupByIndexes).matrix(matrix);
  where groupByIndexes is an array of integers.

  When grouping arcs together, you are taking multiple source arcs and combining them into a single target arc.
  With grouping you can end up with multiple chords that start and stop on the same arc[s].

  Each element in the groupByIndexes array corresponds to a row in the matrix, therefore the array
  should be matrix.length long. The position in the groupByIndexes array specifies the 
  source arc. The value at that position determines the target arc. If the groupByIndexes array has 2 unique
  values (0 and 1) then there will be 2 groups returned.

  For example: With a matrix of 
     [[1,2], 
      [3,4]]
  that represents the trips between 2 neighbourhoods: A and B.
  d3 would normally generate 2 arcs and 3 chords.
  The 1st arc corresponds to A with data of [1.2], and the 2nd to B with data of [3,4].
  Chord 1 would be from A to A with a value of 1.
  Chord 2 would be between A and B with B having a value of 3 and A having a value of 2
  Chord 3 would be from B to B with a value of 4.

  If you had data that splits those same trips into by bike and on foot,
  you could generate a more detailed matrix like:
      [[0,0,0,1],
       [0,1,1,0],
       [0,2,3,0],
       [1,0,0,1]
      ]

  This would generate 4 arcs and 5 chords.
  The chords would be:
  A foot - A foot value 1
  A bike - B bike values 1 and 1
  B foot - A foot values 1 and 2
  B bike - B bike value 3
  B foot - B foot value 1

  But you don't want 4 arcs: A bike, A foot, B bike, and B foot. You want 2 arcs A and B with chords
  between them that represent the bike and foot trips. 
  Even though you could color the A bike and A foot arcs the same, there would still be a gap between them
  and if you switched between the detailed matrix and the aggregate matrix, the arcs would move.
  Also, with 4 arcs, the arc labels could get unruly.

  One possible kludge would be to generate the detailed diagram with 0 padding between arcs and 
  insert dummy rows and columns between the groups.
  The values for the dummy entries would need to be calculated so that their arc size exactly corresponded 
  to the normal padding.
  The arcs and chords for the dummy data would have opacity 0 and not respond to mouse events. You'd also have to 
  create and position the labels separatly from the arcs.

  Or... you could use groupBy.
  The detail matrix would stay the same. The output chords would be the same. The only change would be
  that the arcs A bike and A foot would be combined, and the arcs B bike and B foot would be combined. 
  In the above example you set the groupBy array to  [0,0,1,1].
  This says the 1st two arcs get grouped together into a new arc 0, and the 2nd two arcs get grouped into
  a new arc 1.

  Since there can be chords that have the same source.index and source.subindex and the same target.index and 
  target.subindex, two additional data values  are returned in the chord's source and target data structures: 
  orgindex and orgsubindex. This will let you determine whether the chord is for 
  bike trips or foot trips.
