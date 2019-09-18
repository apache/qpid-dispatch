import React from "react";

import * as d3 from "d3";
import { addDefs } from "./nodes";

class Graph extends React.Component {
  constructor(props) {
    super(props);

    this.state = {};
    this.force = d3.layout
      .force()
      .size([this.props.dimensions.width, this.props.dimensions.height])
      .linkDistance(l => {
        if (this.props.thumbNail) return 40;
        else if (l.type === "router") return 150;
        else if (l.type === "edge") return 20;
        return 50;
      })
      .charge(-800)
      .friction(0.1)
      .gravity(0.001);

    this.mouse_down_position = null;
  }

  // called only once when the component is initialized
  componentDidMount() {
    const svg = d3.select(this.svg);
    if (!this.props.thumbNail && !this.props.legend) {
      addDefs(svg);
    }

    this.force.on("tick", () => {
      // after force calculation starts, call updateGraph
      // which uses d3 to manipulate the attributes,
      // and React doesn't have to go through lifecycle on each tick
      d3.select(this.svgg).call(this.updateGraph);
    });
    // call this manually to create svg circles and lines
    this.shouldComponentUpdate(this.props);
  }

  // called each time one of the properties changes
  shouldComponentUpdate(nextProps) {
    this.d3Graph = d3.select(this.svgg);
    this.appendNodes(this.d3Graph, nextProps, nextProps.nodes);
    this.appendLinks(this.d3Graph, nextProps.links, "connector", ".node");
    this.d3Graph.call(this.updateGraph);

    // warning: d3's force function modifies the nodes and links arrays
    this.force.nodes(nextProps.nodes).links(nextProps.links);
    this.force.start();
    if (nextProps) this.refresh(nextProps);
    return false;
  }

  appendNodes = (selection, nextProps, nodes) => {
    const subNodes = selection.selectAll(".node").data(nodes, node => node.key);
    subNodes
      .enter()
      .append("g")
      .attr("class", "node")
      .attr("id", n => n.key)
      .call(selection => this.enterNode(selection, nextProps));
    subNodes.exit().remove();
    subNodes.call(this.updateNode);
    subNodes.call(this.force.drag);
  };

  appendLinks = (selection, clinks, linkClass, before) => {
    const subLinks = selection
      .selectAll(`.${linkClass}`)
      .data(clinks, link => link.key);
    subLinks
      .enter()
      .insert("g", before)
      .attr("class", linkClass)
      .call(this.enterLink);
    subLinks.exit().remove();
    subLinks.call(this.updateLink);
  };

  // new node/nodes are present
  // append all the stuff and set the attributes that don't change
  enterNode = (selection, props) => {
    const graph = this;
    const routers = selection.filter(d => d.type === "interior");
    const edges = selection.filter(d => d.type === "edgeClass");

    selection.append("circle").attr("r", d => d.r);

    routers.append("path").attr("d", d =>
      d3.svg
        .arc()
        .innerRadius(0)
        .outerRadius(d.r)({
        startAngle: 0,
        endAngle: (d.state * 2.0 * Math.PI) / 3.0
      })
    );

    selection
      .classed("edgeClass", d => d.type === "edgeClass")
      .classed("interior", d => d.type === "interior");

    if (!props.thumbNail || props.legend) {
      selection.classed("network", true);
    }
    if (!props.thumbNail) {
      selection
        .append("text")
        .attr("x", d => d.r + 5)
        .attr("dy", ".35em")
        .text(d => d.Name);
      edges
        .append("text")
        .classed("edge-count", true)
        .attr("x", -24)
        .attr("dy", "0.35em")
        .text(""); // updated in refresh
    }
    /* // this creates an octagon
    const sqr2o2 = Math.sqrt(2.0) / 2.0;
    const points = `1 0 ${sqr2o2} ${sqr2o2} 0 1 -${sqr2o2} ${sqr2o2} -1 0 -${sqr2o2} -${sqr2o2} 0 -1 ${sqr2o2} -${sqr2o2}`;
    selection
      .filter(d => d.type === "edgeClass")
      .append("polygon")
      .attr("points", points)
      .attr("transform", `scale(60) rotate(22.5)`);
*/
    selection
      .on("mouseover", function(n) {
        if (graph.props.thumbNail) return;
        n.over = true;
        graph.updateNode(d3.select(this));
      })
      .on("mouseout", function(n) {
        if (graph.props.thumbNail) return;
        n.over = false;
        graph.updateNode(d3.select(this));
      })
      .on("click", function(n) {
        if (graph.props.thumbNail) return;
        // if there was a selected node and it was not the one we just clicked on:
        // create a link between the selected node and the clicked on node
        if (graph.props.selectedKey && graph.props.selectedKey !== n.key) {
          graph.props.notifyCreateLink(n.key, graph.props.selectedKey);
        }

        // see if the node was dragged (same === false)
        const same = graph.samePos(
          d3.mouse(this.parentNode),
          graph.mouse_down_position,
          "node"
        );
        if (same) {
          if (graph.props.selectedKey === n.key) {
            graph.props.notifyCurrentRouter(null);
          } else {
            graph.props.notifyCurrentRouter(n.key);
          }
        } else {
          graph.props.notifyCurrentRouter(n.key);
        }
        graph.refresh(graph.props);
      })
      .on("mousedown", function(n) {
        graph.mouse_down_position = d3.mouse(this.parentNode);
        graph.draggingNode = true;
      })
      .on("mouseup", n => {
        if (graph.props.thumbNail) return;
        if (n.type !== "edge") n.fixed = true;
      });
  };

  samePos = (pos1, pos2, where) => {
    if (pos1 && pos2) {
      if (pos1[0] === pos2[0] && pos1[1] === pos2[1]) return true;
    }
    return false;
  };

  arcTween = (oldData, newData, arc) => {
    const copy = { ...oldData };
    return function() {
      const interpolateStartAngle = d3.interpolate(
          oldData.startAngle,
          newData.startAngle
        ),
        interpolateEndAngle = d3.interpolate(
          oldData.endAngle,
          newData.endAngle
        );

      return function(t) {
        copy.startAngle = interpolateStartAngle(t);
        copy.endAngle = interpolateEndAngle(t);
        return arc(copy);
      };
    };
  };
  // called each time a property changes
  // update the classes/text based on the new properties
  refresh = props => {
    if (props.thumbNail) return;
    d3.selectAll("g.node.network").classed(
      "selected",
      d => d.key === props.selectedKey
    );

    // update the interior node state
    d3.selectAll("g.node.network.interior path").attr("d", d =>
      d3.svg
        .arc()
        .innerRadius(0)
        .outerRadius(d.r)({
        startAngle: 0,
        endAngle: (d.state * 2.0 * Math.PI) / 3.0
      })
    );

    d3.selectAll("svg text").each(function(d) {
      d3.select(this).text(d.Name);
    });
    d3.selectAll("g.connector").classed(
      "selected",
      d => d.key === props.selectedKey
    );
    d3.selectAll("text.edge-count").text(d => {
      return d.rows.length > 0 ? `Edges: ${d.rows.length}` : "";
    });
  };

  // update the node's positions
  updateNode = selection => {
    selection.attr("transform", d => {
      let container = {
        width: this.props.dimensions.width,
        height: this.props.dimensions.height
      };
      let r = 15;
      d.x = Math.max(Math.min(d.x, container.width - r), r);
      d.y = Math.max(Math.min(d.y, container.height - r), r);
      return `translate(${d.x || 0},${d.y || 0}) ${d.over ? "scale(1.1)" : ""}`;
    });
  };

  markerId = (link, end) => {
    return `--${end === "end" ? link.size : link.size}`;
  };

  // called with a selection that represents all the new links between nodes
  // here we add the lines and set their attributes
  enterLink = selection => {
    const graph = this;

    // add a visible line with an arrow
    selection
      .append("path")
      .classed("link", true)
      .attr("stroke-width", d => d.size)
      .attr("marker-end", d => {
        return d.right ? `url(#end--20)` : null;
      })
      .attr("marker-start", d => {
        if (d.type === "edge") return null;
        if (this.props.thumbNail) return null;
        return d.left || (!d.left && !d.right) ? `url(#start--20)` : null;
      });

    if (!this.props.thumbNail && !this.props.legend) {
      // add an invisible wide path to make it easier to mouseover
      selection
        .append("path")
        .classed("hittarget", true)
        .on("click", function(d) {
          d3.select(this.parentNode).classed("selected", true);
          graph.notifyCurrentConnector(d);
          graph.refresh(graph.props);
        })
        .on("mouseover", function(n) {
          d3.select(this.parentNode).classed("over", true);
        })
        .on("mouseout", function(n) {
          d3.select(this.parentNode).classed("over", false);
        });
    }
    this.refresh(this.props);
  };

  notifyCurrentConnector = d => {
    if (this.props.notifyCurrentConnector) this.props.notifyCurrentConnector(d);
  };

  // update the links' positions
  updateLink = selection => {
    const stxy = d => {
      let sx = d.source.x || this.props.nodes[d.source].x;
      let tx = d.target.x || this.props.nodes[d.target].x;
      let sy = d.source.y || this.props.nodes[d.source].y;
      let ty = d.target.y || this.props.nodes[d.target].y;

      if (d.source.parentKey !== d.target.parentKey) {
        const snode = this.props.nodes.find(n => n.key === d.source.parentKey);
        const tnode = this.props.nodes.find(n => n.key === d.target.parentKey);
        if (snode && tnode) {
          sx += snode.kx;
          tx += tnode.kx;
          sy += snode.ky;
          ty += tnode.ky;
        }
      }
      const deltaX = tx - sx;
      const deltaY = ty - sy;
      const dist = Math.sqrt(deltaX * deltaX + deltaY * deltaY);
      const normX = deltaX / dist;
      const normY = deltaY / dist;
      const sourcePadding = d.source.r || this.props.nodes[d.source].r;
      const targetPadding = d.target.r || this.props.nodes[d.target].r;
      const sourceX = sx + sourcePadding * normX;
      const sourceY = sy + sourcePadding * normY;
      const targetX = tx - targetPadding * normX;
      const targetY = ty - targetPadding * normY;
      return { x1: sourceX, y1: sourceY, x2: targetX, y2: targetY };
    };
    selection.attr("d", d => {
      const endp = stxy(d);
      return `M${endp.x1},${endp.y1}L${endp.x2},${endp.y2}`;
    });
  };

  // called each animation tick to update the positions
  updateGraph = selection => {
    selection.selectAll(".node").call(this.updateNode);
    selection.selectAll(".link").call(this.updateLink);
    selection.selectAll(".hittarget").call(this.updateLink);
  };

  render() {
    const { width, height } = this.props.dimensions;
    return (
      <React.Fragment>
        <svg
          width={width}
          height={height}
          ref={el => (this.svg = el)}
          xmlns="http://www.w3.org/2000/svg"
        >
          <g ref={el => (this.svgg = el)} />
        </svg>
      </React.Fragment>
    );
  }
}

export default Graph;
