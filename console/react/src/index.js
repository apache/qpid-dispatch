import React from "react";
import ReactDOM from "react-dom";
import App from "./App";
import * as serviceWorker from "./serviceWorker";

let config = { title: "Apache Qpid Dispatch Console" };
fetch("/config.json")
  .then(res => res.json())
  .then(cfg => {
    config = cfg;
    console.log("successfully loaded console title from /config.json");
  })
  .catch(error => {
    console.log("/config.json not found. Using default console title");
  })
  .finally(() =>
    ReactDOM.render(<App config={config} />, document.getElementById("root"))
  );

// If you want your app to work offline and load faster, you can change
// unregister() to register() below. Note this comes with some pitfalls.
// Learn more about service workers: https://bit.ly/CRA-PWA
serviceWorker.unregister();
