
    queue()
	.defer(d3.text, './results/tests/2015/11/06/p2p_soak/C/receiver/tp_throughput')
	.defer(d3.text, './results/tests/2015/11/05/p2p_soak/C/receiver/throughput')
	.await(makeChart);

var makeChart = function (error, diff, date2) {

  if (error) {
      document.write("Error reading file");
      return;
  }
  var rows = diff.split('\n');
  var columns = [];
  columns.push(['x']);
  columns.push(['Nov 6 2015']);
  rows.forEach( function (row) {
     var cols = row.split(' ');
     cols = cols.filter( function (col) {return col!=""});
     if (cols[1]) columns[1].push(parseInt(cols[1]));
     if (cols[0]) columns[0].push(parseInt(cols[0])/1000000);
  });
  console.log(JSON.stringify(columns));
  var chart = c3.generate({
    data: {
        x:       'x',
        columns: columns,
        type: 'line'
    },
    axis: {
        x: {
            type: 'indexed',
            tick: {
                values: [0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100]
            }
        },
        y: {
            label: {
                text: 'Messages per second',
                position: 'outer-middle'
            },
            min: 50000,
            max: 500000
        }
    },
    legend: {
        position: 'inset',
        inset: {
            anchor: 'top-right',
            x: 18,
            y: 1,
            step: 1
        }
    },
    grid: {
      x: {
        show: false
      },
      y: {
        show: false
      }
    },
    tooltip: {
        format: {
            title: function (d) { return 'After ' + d + ' million' }
        }
    }
  });

};

