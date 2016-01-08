        var initGlobe = function (urlPrefix) {
			d3.select(window)
				.on("mousemove", mousemove)
				.on("mouseup", mouseup);

			var width = 960,
				height = 500;

			var proj = d3.geo.orthographic()
				.scale(220)
				.translate([width / 2, height / 2])
				.clipAngle(90);

			var path = d3.geo.path().projection(proj).pointRadius(1.5);

			var links = [],
				arcLines = [];

			var graticule = d3.geo.graticule();
			var svg = d3.select("#geology").append("svg")
				.attr("width", width)
				.attr("height", height)
				.on("mousedown", mousedown);

			queue()
				.defer(d3.json, "plugin/data/world-110m.json")
				.defer(d3.json, "plugin/data/places1.json")
				.await(ready);

			function ready(error, world, places) {
			  var ocean_fill = svg.append("defs").append("radialGradient")
					.attr("id", "ocean_fill")
					.attr("cx", "75%")
					.attr("cy", "25%");
				  ocean_fill.append("stop").attr("offset", "5%").attr("stop-color", "#fff");
				  ocean_fill.append("stop").attr("offset", "100%").attr("stop-color", "#eef");

			  var globe_highlight = svg.append("defs").append("radialGradient")
					.attr("id", "globe_highlight")
					.attr("cx", "75%")
					.attr("cy", "25%");
				  globe_highlight.append("stop")
					.attr("offset", "5%").attr("stop-color", "#ffd")
					.attr("stop-opacity","0.6");
				  globe_highlight.append("stop")
					.attr("offset", "100%").attr("stop-color", "#ba9")
					.attr("stop-opacity","0.1");

			  var globe_shading = svg.append("defs").append("radialGradient")
					.attr("id", "globe_shading")
					.attr("cx", "55%")
					.attr("cy", "45%");
				  globe_shading.append("stop")
					.attr("offset","30%").attr("stop-color", "#fff")
					.attr("stop-opacity","0")
				  globe_shading.append("stop")
					.attr("offset","100%").attr("stop-color", "#505962")
					.attr("stop-opacity","0.2")

			  var drop_shadow = svg.append("defs").append("radialGradient")
					.attr("id", "drop_shadow")
					.attr("cx", "50%")
					.attr("cy", "50%");
				  drop_shadow.append("stop")
					.attr("offset","20%").attr("stop-color", "#000")
					.attr("stop-opacity",".5")
				  drop_shadow.append("stop")
					.attr("offset","100%").attr("stop-color", "#000")
					.attr("stop-opacity","0")

			  svg.append("ellipse")
				.attr("cx", 440).attr("cy", 450)
				.attr("rx", proj.scale()*.90)
				.attr("ry", proj.scale()*.25)
				.attr("class", "noclicks")
				.style("fill", "url("+urlPrefix+"#drop_shadow)");

			  svg.append("circle")
				.attr("cx", width / 2).attr("cy", height / 2)
				.attr("r", proj.scale())
				.attr("class", "noclicks")
				.style("fill", "url("+urlPrefix+"#ocean_fill)");

			  svg.append("path")
				.datum(topojson.object(world, world.objects.land))
				.attr("class", "land noclicks")
				.attr("d", path);

			  svg.append("path")
				.datum(graticule)
				.attr("class", "graticule noclicks")
				.attr("d", path);

			  svg.append("circle")
				.attr("cx", width / 2).attr("cy", height / 2)
				.attr("r", proj.scale())
				.attr("class","noclicks")
				.style("fill", "url("+urlPrefix+"#globe_highlight)");

			  svg.append("circle")
				.attr("cx", width / 2).attr("cy", height / 2)
				.attr("r", proj.scale())
				.attr("class","noclicks")
				.style("fill", "url("+urlPrefix+"#globe_shading)");

			  svg.append("g").attr("class","points")
				  .selectAll("text").data(places.features)
				.enter().append("path")
				  .attr("class", "point")
				  .attr("d", path);

				svg.append("g").attr("class","labels")
					.selectAll("text").data(places.features)
				  .enter().append("text")
				  .attr("class", "label")
				  .text(function(d) { return d.properties.NAME })

				position_labels();

			  // spawn links between cities as source/target coord pairs
			  places.features.forEach(function(a, i) {
				places.features.forEach(function(b, j) {
				  if (j > i) {	// avoid duplicates
					links.push({
					  source: a.geometry.coordinates,
					  target: b.geometry.coordinates
					});
				  }
				});
			  });

			  // build geoJSON features from links array
			  links.forEach(function(e,i,a) {
				var feature =   { "type": "Feature", "geometry": { "type": "LineString", "coordinates": [e.source,e.target] }}
				arcLines.push(feature)
			  })

			  svg.append("g").attr("class","arcs")
				.selectAll("path").data(arcLines)
				.enter().append("path")
				  .attr("class","arc")
				  .attr("d",path)
			  refresh();
			}

			function position_labels() {
			  var centerPos = proj.invert([width/2,height/2]);

			  var arc = d3.geo.greatArc();

			  svg.selectAll(".label")
				.attr("transform", function(d) {
				  var loc = proj(d.geometry.coordinates),
					x = loc[0],
					y = loc[1];
				  var offset = x < width/2 ? -5 : 5;
				  return "translate(" + (x+offset) + "," + (y-2) + ")"
				})
				.style("display",function(d) {
				  var d = arc.distance({source: d.geometry.coordinates, target: centerPos});
				  return (d > 1.57) ? 'none' : 'inline';
				})

			}

			function refresh() {
			  svg.selectAll(".land").attr("d", path);
			  svg.selectAll(".point").attr("d", path);
			  svg.selectAll(".graticule").attr("d", path);
			  svg.selectAll(".arc").attr("d", path);
			  position_labels();
			}

			// modified from http://bl.ocks.org/1392560
			var m0, o0;
			o0 = angular.fromJson(localStorage["QDR.rotate"]);
			if (o0)
				proj.rotate(o0);

			function mousedown() {
			  m0 = [d3.event.pageX, d3.event.pageY];
			  o0 = proj.rotate();
			  d3.event.preventDefault();
			}
			function mousemove() {
			  if (m0) {
				var m1 = [d3.event.pageX, d3.event.pageY]
				  , o1 = [o0[0] + (m1[0] - m0[0]) / 6, o0[1] + (m0[1] - m1[1]) / 6];
				o1[1] = o1[1] > 30  ? 30  :
						o1[1] < -30 ? -30 :
						o1[1];
				proj.rotate(o1);
				refresh();
			  }
			}
			function mouseup() {
			  if (m0) {
				mousemove();
				m0 = null;
				localStorage["QDR.rotate"] = angular.toJson(proj.rotate());
			  }
			}
        }
