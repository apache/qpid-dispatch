const express = require("express");
const app = express();
const path = require("path");
const bodyParser = require("body-parser");

app.use(bodyParser.json());
app.use(bodyParser.urlencoded({ extended: false }));

/**
 * API
 */
// Not used at this time
app.get("/api", function(req, res) {
  console.log("GET request");
  console.log(req.query);
  let response = { message: "unknown get request" };
  let query = req.query;
  if (query.n && query.nn) {
    // get the state of router with name n from network with name nn
    response = { state: 1 };
  }
  res.status(200).send(response);
});

// handle a POST.
app.post("/api", function(req, res) {
  console.log("POST request received");
  let status = 200;
  let response = { message: "unable to parse request" };
  // figure out what to do based on what is in data

  // get the info that was passed in
  let { what } = req.body;
  if (what === "saveNetwork") {
    response = saveNetwork(req.body);
  } else if (what === "getState") {
    response = getState(req.body);
  } else {
    response = {
      status: 404,
      response: { message: "Missing 'what' parameter in POST request" }
    };
    console.log(response.message);
  }
  if (response.status) {
    status = response.status;
    response = response.response;
  }
  res.status(status).send(response);
});

const edgeDeployment = body => {
  // network is an object that has a nodes array, a links array, and a network name
  // nodeName is the node.Name of the router we are requesting data for
  const { router } = body;
  const nodeName = router.Name;
  console.log(`request was for edge-deployment info for ${nodeName}`);
  return { deployment: `handle edge-deployment for ${nodeName}` };
};

const routerDeployment = body => {
  console.log("request was for router deployment info");
  let { router } = body;
  // router is the object that contains the info entered about a router
  // router should contain Name, State

  // do some validation and construct the response
  if (router && router.Name && router.Name !== "") {
    // This is NOT really what we want to return. This is just an example.
    const response = {
      apiVersion: "interconnectedcloud.github.io/v1alpha1",
      kind: "Interconnect",
      metadata: {
        name: "example-interconnect"
      },
      spec: {
        deploymentPlan: {
          image: "quay.io/interconnectedcloud/qdrouterd:1.7.0",
          role: "interior",
          size: 3,
          placement: "Any"
        }
      }
    };
    return response;
  }
  return { status: 404, response: { message: "Unable to find router" } };
};

/*
saveNetwork creates yaml that looks like the following:
Router $Name inter-router.$PROJECT.$ROUTING_SUFFIX
EdgeRouter $Name
Connect $Name $Name
# Console $Name console.$PROJECT.$ROUTING_SUFFIX
Console PVT console.skuba.127.0.0.1.nip.io
*/ const saveNetwork = body => {
  console.log("request was to save current network");
  const { network } = body;
  const yaml = [];
  let consoleCreated = false;
  network.nodes.forEach(n => {
    if (n.type === "interior") {
      // namespace and suffix are optional
      yaml.push(`Router ${n.Name} inter-router`);
      // create a console for the 1st interior router
      if (!consoleCreated) {
        consoleCreated = true;
        yaml.push(`Console ${n.Name} console`);
      }
    } else if (n.type === "edgeClass") {
      if (n.rows) {
        n.rows.forEach(r => {
          yaml.push(`EdgeRouter ${r.name}`);
        });
      }
    }
  });
  network.links.forEach(l => {
    if (l.source.type === "interior" && l.target.type === "interior") {
      yaml.push(`Connect ${l.source.Name} ${l.target.Name}`);
    } else if (l.target.type === "edgeClass") {
      // target is an edgeClass
      // push a link for each edge router in the edgeclass
      const edgeClass = l.target;
      edgeClass.rows.forEach(r => {
        yaml.push(`Connect ${r.name} ${l.source.Name}`);
      });
    }
  });
  console.log("Current network yaml:");
  console.log(yaml.join("\\n"));
  return {
    yaml: yaml.join("\n")
  };
};

const getState = body => {
  const { router } = body;
  let deployment;
  let status = 200;
  console.log("getState requested for");
  let state = router.type === "edge" ? router.row.state : router.state;
  // if the router is NEW, get the deployment yaml/text
  if (state === 0) {
    console.log(router);
    deployment =
      router.type === "edge" ? edgeDeployment(body) : routerDeployment(body);
    if (deployment.status) {
      status = deployment.status;
      deployment = deployment.response;
    }
  }
  // TODO: get the router state using the router info
  return {
    status: status,
    response: { state: state, deployment: deployment }
  };
};

/**
 * STATIC FILES
 */
app.use("/", express.static("./"));

// Default every route except the above to serve the index.html
app.get("*", function(req, res) {
  res.sendFile(path.join(__dirname + "/index.html"));
});

module.exports = app;
