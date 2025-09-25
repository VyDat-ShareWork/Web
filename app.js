function formatDateTime(ts) {
    const d = new Date(ts);
    const dd = String(d.getDate()).padStart(2, "0");
    const mm = String(d.getMonth() + 1).padStart(2, "0");
    const yyyy = d.getFullYear();
    const hh = String(d.getHours()).padStart(2, "0");
    const mi = String(d.getMinutes()).padStart(2, "0");
    const ss = String(d.getSeconds()).padStart(2, "0");
    return `${dd}-${mm}-${yyyy} ${hh}:${mi}:${ss}`;
}

function makeLine(ctx, border, fill) {
    return new Chart(ctx, {
        type: "line",
        data: { labels: [], datasets: [{ data: [], borderColor: border, backgroundColor: fill, fill: "origin", tension: .25 }] },
        options: {
            responsive: true, maintainAspectRatio: false, animation: false,
            plugins: { legend: { display: false }, decimation: { enabled: true, algorithm: "min-max", samples: 120 } },
            scales: { x: { display: true }, y: { beginAtZero: true } }
        }
    });
}

const rmsChart   = makeLine(document.getElementById("rmsChart"),  "rgba(109, 253, 210, 1)",   "rgba(0, 255, 157, 0.1)");
const peakChart  = makeLine(document.getElementById("peakChart"), "rgba(253, 160, 180, 1)",  "rgba(255,99,132,.15)");
const p2pChart   = makeLine(document.getElementById("p2pChart"),  "rgba(214, 212, 86, 1)",   "rgba(198, 207, 65, 0.12)");
const crestChart = makeLine(document.getElementById("crestChart"),"rgba(181, 144, 255, 1)", "rgba(153,102,255,.15)");

const freqChart = new Chart(document.getElementById("freqChart"), {
    type: "bar",
    data: { labels: [], datasets: [{ data: [], label: "Magnitude", backgroundColor: "#58c6f1ff", barPercentage: 0.4, categoryPercentage: 0.6 }] },
    options: { responsive: true, maintainAspectRatio: false, plugins: { legend: { display: false } }, scales: { y: { beginAtZero: true } } }
});

const MAX_POINTS = 30;
function pushPoint(chart, x, y) {
    chart.data.labels.push(x);
    chart.data.datasets[0].data.push(y ?? 0);
    if (chart.data.labels.length > MAX_POINTS) {
        chart.data.labels.shift(); chart.data.datasets[0].data.shift();
    }
}
const el = id => document.getElementById(id);
const fmt = v => (v == null ? "--" : Number(v).toFixed(3));

function setStatusBadge(text) {
    const b = el("kpiStatus");
    b.textContent = text || "--";
    b.className = "badge " + (text === "Normal" ? "normal" : (text === "Warning" ? "warn" : "fault"));
}

function addOverviewRow(d) {
    const tbody = document.getElementById("overviewBody");
    if (!tbody) return;

    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td>${d.timestamp ? formatDateTime(d.timestamp) : "--"}</td>
      <td>${fmt(d.rms)}</td>
      <td>${fmt(d.peak)}</td>
      <td>${fmt(d.peak_to_peak)}</td>
      <td>${fmt(d.crest_factor)}</td>
      <td>${d.status || "--"}</td>
      <td>${d.confident != null ? (d.confident * 100).toFixed(1) : "--"}</td>
    `;
    tbody.prepend(tr);
}

const brokerUrl = "wss://de6686b6c05c4c2985ed2a5c67d01b2c.s1.eu.hivemq.cloud:8884/mqtt";
const options = { username: "hieuvm", password: "Hieu1234@!", protocol: "wss" };
const topic ="Node1"; 

const client = mqtt.connect(brokerUrl, options);

client.on("connect", () => {
    console.log("Connected to HiveMQ Cloud");
    client.subscribe(topic, (err) => {
        if (err) console.error("Subscribe error:", err);
        else console.log("Subscribed to:", topic);
    });
});

let tick = 0;
client.on("message", (_t, msg) => {
    try {
        const data = JSON.parse(msg.toString());
        tick++;
        updateChartsAndUI(data);
    } catch (e) {
        console.error("Parse error:", e);
    }
});

function updateChartsAndUI(data) {
    // Charts
    pushPoint(rmsChart, tick, data.rms);
    pushPoint(peakChart, tick, data.peak);
    pushPoint(p2pChart, tick, data.peak_to_peak);
    pushPoint(crestChart, tick, data.crest_factor);
    rmsChart.update(); peakChart.update(); p2pChart.update(); crestChart.update();

    if (Array.isArray(data.dominant_frequencies) && Array.isArray(data.frequency_magnitudes)) {
        freqChart.data.labels = data.dominant_frequencies.map(f => Number(f).toFixed(1));
        freqChart.data.datasets[0].data = data.frequency_magnitudes;
        freqChart.update();
    }

    // KPI cards
    el("kpiRms").textContent   = fmt(data.rms);
    el("kpiPeak").textContent  = fmt(data.peak);
    el("kpiCrest").textContent = fmt(data.crest_factor);
    el("kpiConf").textContent  = data.confident != null ? (data.confident * 100).toFixed(1) + "%" : "--";
    setStatusBadge(data.status);

    // System status box
    const statusEl = el("statusText");
    const statusVal = data.status || "--";
    statusEl.textContent = statusVal;
    statusEl.className = "badge";
    if (statusVal === "Normal")      statusEl.classList.add("status-normal");
    else if (statusVal === "Warning") statusEl.classList.add("status-warning");
    else                              statusEl.classList.add("status-fault");

    el("confText").textContent  = (data.confident != null ? (data.confident * 100).toFixed(1) : "--") + "%";
    el("sampleText").textContent = tick;
    el("tsText").innerHTML    = data.timestamp ? formatDateTime(data.timestamp) : formatDateTime(new Date());

    if (data.dominant_frequencies) {
        el("freqText").textContent = data.dominant_frequencies.map(f => Number(f).toFixed(1)).join(", ") + " Hz";
    }

    addOverviewRow(data);
}

// ===================== FAKE DATA TEST =====================
// setInterval(() => {
//     tick++;
//     const fake = {
//         timestamp: new Date().toISOString(),
//         rms: Math.random() * 2,
//         peak: Math.random() * 5,
//         peak_to_peak: Math.random() * 10,
//         crest_factor: Math.random() * 3,
//         status: ["Normal", "Warning", "Fault"][Math.floor(Math.random() * 3)],
//         confident: Math.random() * 0.2 + 0.8,
//         dominant_frequencies: [10, 20, 30, 40, 50].map(f => f + Math.random() * 2),
//         frequency_magnitudes: Array.from({ length: 5 }, () => Math.random() * 5)
//     };
//     updateChartsAndUI(fake);
// }, 1500);
