import React from "react";
import Graph from "./graph";

class ShowD3SVG extends React.Component {
  constructor(props) {
    super(props);
    this.state = this.setNodesLinks();
  }

  // if the number of routers has changed
  // recreate the nodes and links arrays
  componentDidUpdate(prevProps) {
    if (this.props.routers !== prevProps.routers) {
      this.setState(this.addClients(this.setNodesLinks()));
    }
  }

  addClients = state => {
    const { nodes, links } = state;
    const midX = this.props.dimensions.width / 2;
    const midY = this.props.dimensions.height / 2;
    for (const r in this.props.routerInfo) {
      if (this.props.routerInfo.hasOwnProperty(r)) {
        const info = this.props.routerInfo[r];
        const parent = nodes.find(n => n.name === r);
        if (parent) {
          // addNodesInCircle = (nodes, start, count, midX, midY, displace, rotate) => {
          info.forEach((inf, i) => {
            const node = this.node(nodes.length, midX, midY);
            node.parent = parent.val;
            node.key = `${node.key}.C${i}`;
            node.name = `C${i}`;
            node.type = inf.client;
            const { x, y } = this.getXY(
              i,
              info.length,
              parent.x,
              parent.y,
              0,
              40
            );
            node.x = x;
            node.y = y;
            nodes.push(node);
            const l = this.link(parent.val, node.val);
            l.type = "client";
            links.push(l);
          });
        }
      }
    }
    return state;
  };

  link = (s, t) => ({
    source: s,
    target: t,
    key: `${s}:${t}`,
    size: 2,
    type: "router"
  });
  setLinks = (topology, start, count) => {
    let links = [];
    if (topology === "linear") {
      for (let i = start; i < start + count - 1; i++) {
        links.push(this.link(i, i + 1));
      }
    } else if (topology === "mesh") {
      for (let i = start; i < start + count - 1; i++) {
        for (let j = i + 1; j < start + count; j++) {
          links.push(this.link(i, j));
        }
      }
    } else if (topology === "star") {
      for (let i = start; i < start + count; i++) {
        links.push(this.link(start, i));
      }
    } else if (topology === "ring") {
      for (let i = start; i < start + count - 1; i++) {
        links.push(this.link(i, i + 1));
      }
      if (start + count > 2) links.push(this.link(start + count - 1, start));
    } else if (topology === "ted") {
      if (count < 3) {
        links = this.setLinks("linear", 0, count);
      } else {
        links = this.setLinks("mesh", 2, count - 2);
        links.push(this.link(0, 2));
        links.push(this.link(0, count - 1));
        links.push(this.link(1, Math.floor((count - 2) / 2) + 1));
        links.push(this.link(1, Math.floor((count - 2) / 2) + 2));
      }
    } else if (topology === "bar_bell") {
      links.push(this.link(0, Math.ceil(count / 2)));
      links = links.concat(this.setLinks("ring", 0, Math.ceil(count / 2)));
      links = links.concat(
        this.setLinks("ring", Math.ceil(count / 2), Math.floor(count / 2))
      );
    } else if (topology === "random") {
      // random int from min to max inclusive
      let randomIntFromInterval = (min, max) =>
        Math.floor(Math.random() * (max - min + 1) + min);
      // are two nodes already connected
      let isConnected = (s, t) => {
        if (s === t) return true;
        return links.some(l => {
          return (
            (l.source === s && l.target === t) ||
            (l.target === s && l.source === t)
          );
        });
      };

      // connect all nodes
      for (let i = 1; i < this.props.routers; i++) {
        let source = randomIntFromInterval(0, i - 1);
        links.push(this.link(source, i));
      }
      // randomly add n-1 connections
      for (let i = 0; i < this.props.routers - 1; i++) {
        let source = randomIntFromInterval(0, this.props.routers - 1);
        let target = randomIntFromInterval(0, this.props.routers - 1);
        if (!isConnected(source, target)) {
          links.push(this.link(source, target));
        }
      }
    }
    return links;
  };

  node = (i, x, y) => ({
    key: `key_${i}`,
    name: `R${i}`,
    val: i,
    size: this.props.radius ? this.props.radius : 15,
    x: x,
    y: y,
    parentKey: "cluster",
    r: 8
  });

  getXY = (start, count, midX, midY, rotate, displace) => {
    const ang = (start * 2.0 * Math.PI) / count + rotate;
    const x = midX + Math.cos(ang) * displace;
    const y = midY + Math.sin(ang) * displace;
    return { x, y };
  };

  addNodesInCircle = (nodes, start, count, midX, midY, displace, rotate) => {
    rotate = rotate || 0;
    for (let i = start; i < start + count; i++) {
      const { x, y } = this.getXY(
        i - start,
        count,
        midX,
        midY,
        rotate,
        displace
      );
      nodes.push(this.node(i, x, y));
    }
  };
  setNodesLinks = () => {
    const nodes = [];
    let links = [];
    const midX = this.props.dimensions.width / 2;
    const midY = this.props.dimensions.height / 2;
    const displace = this.props.dimensions.height / 2;

    // create the routers
    // set their starting positions in a circle
    if (this.props.topology === "ted" && this.props.routers > 1) {
      nodes.push(this.node(0, this.props.dimensions.width, midY));
      nodes.push(this.node(1, 0, midY));
      this.addNodesInCircle(
        nodes,
        2,
        this.props.routers - 2,
        midX,
        midY,
        displace,
        Math.PI / (this.props.routers - 2)
      );
    } else if (this.props.topology === "bar_bell" && this.props.routers > 1) {
      this.addNodesInCircle(
        nodes,
        0, // start
        Math.ceil(this.props.routers / 2), // count
        this.props.dimensions.width / 4, // midX
        midY, // midY
        displace / 3 // displace
      );
      this.addNodesInCircle(
        nodes,
        Math.ceil(this.props.routers / 2),
        Math.floor(this.props.routers / 2),
        this.props.dimensions.width * 0.75,
        midY,
        displace / 3,
        Math.PI
      );
    } else if (this.props.center && this.props.routers > 1) {
      nodes.push(this.node(0, midX, midY));
      this.addNodesInCircle(
        nodes,
        1,
        this.props.routers - 1,
        midX,
        midY,
        displace
      );
    } else if (this.props.routers === 1) {
      nodes.push(this.node(0, midX, midY));
    } else {
      this.addNodesInCircle(nodes, 0, this.props.routers, midX, midY, displace);
    }
    if (this.props.routers > 1)
      links = this.setLinks(this.props.topology, 0, this.props.routers);
    return { nodes: nodes, links: links };
  };

  render() {
    const { nodes, links } = this.state;
    return (
      <Graph
        nodes={nodes}
        links={links}
        dimensions={this.props.dimensions}
        thumbNail={this.props.thumbNail}
        notifyCurrentRouter={this.props.notifyCurrentRouter}
      />
    );
  }
}

export default ShowD3SVG;
