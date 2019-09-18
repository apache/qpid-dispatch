class NodesLinks {
  static nextNodeIndex = 1;
  static nextLinkIndex = 1;
  static nextEdgeIndex = 1;
  static nextEdgeClassIndex = 1;
  link = (s, t, i) => ({
    source: s,
    target: t,
    key: `link-${i}`,
    size: 2,
    type: "connector",
    left: true,
    right: false
  });
  setLinks = (start, count) => {
    let links = [];
    for (let i = start; i < start + count - 1; i++) {
      links.push(this.link(i, i + 1, NodesLinks.nextLinkIndex++));
    }
    return links;
  };

  node = (type, i, x, y) => ({
    key: `key_${type}_${i}`,
    val: i,
    r: 20,
    x: x,
    y: y,
    type: type
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
      nodes.push(this.node("R", i, x, y));
    }
  };

  addNode = (type, networkInfo, dimensions) => {
    let i;
    if (type === "edgeClass") {
      i = NodesLinks.nextEdgeClassIndex++;
    } else {
      i = NodesLinks.nextNodeIndex++;
    }
    const x = dimensions.width / 2;
    const y = dimensions.height / 2;
    const newNode = this.node(type, i, x, y);
    newNode.type = type;
    if (type === "interior") {
      newNode.Name = `hub-${i}`;
      newNode["Route-suffix"] = "";
      newNode.Namespace = "";
      newNode.state = 0;
    } else if (type === "edgeClass") {
      newNode.Name = `EC-${i}`;
      newNode.rows = [];
      newNode.r = 60;
    }
    networkInfo.nodes.push(newNode);
    return newNode;
  };

  addLink = (toIndex, fromIndex, links, nodes) => {
    if (!this.linkBetween(links, toIndex, fromIndex)) {
      const link = this.link(fromIndex, toIndex, NodesLinks.nextLinkIndex++);
      if (
        nodes[toIndex].type === "interior" &&
        nodes[fromIndex].type === "interior"
      ) {
        link["connector type"] = "inter-router";
      } else {
        link["connector type"] = "edge";
      }
      link.connector = () => nodes[toIndex].Name;
      link.listener = () => nodes[fromIndex].Name;
      links.push(link);
      return link;
    }
  };

  getEdgeName = () => {
    return `edge-${NodesLinks.nextEdgeIndex++}`;
  };
  getEdgeKey = () => {
    return NodesLinks.nextEdgeIndex;
  };

  // return true if there are any links between toIndex and fromIndex
  linkBetween = (links, toIndex, fromIndex) => {
    return links.some(
      l =>
        (l.source.index === toIndex && l.target.index === fromIndex) ||
        (l.source.index === fromIndex && l.target.index === toIndex)
    );
  };
  linkIndex = (links, nodeIndex) => {
    return links.findIndex(
      l => l.source.index === nodeIndex || l.target.index === nodeIndex
    );
  };

  setNodesLinks = (networkInfo, dimensions) => {
    const nodes = [];
    let links = [];
    const midX = dimensions.width / 2;
    const midY = dimensions.height / 2;
    const displace = dimensions.height / 2;
    // create the routers
    // set their starting positions in a circle
    if (networkInfo.routers.length === 1) {
      nodes.push(this.node("R", 0, midX, midY));
    } else {
      this.addNodesInCircle(
        nodes,
        0,
        networkInfo.routers.length,
        midX,
        midY,
        displace
      );
    }
    if (networkInfo.routers.length > 1)
      links = this.setLinks(0, networkInfo.routers.length);
    return { nodes, links };
  };
}

export default NodesLinks;
