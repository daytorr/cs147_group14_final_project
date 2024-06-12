from flask import Flask, request, send_file, jsonify
import time

app = Flask(__name__)

data = []
latest_var = ""

@app.route("/")
def hello():
    global latest_var
    var = request.args.get("var")
    steps = request.args.get("steps")
    if var:
        latest_var = var
    if steps is not None:
        timestamp = int(time.time())
        data.append((timestamp, int(steps)))
    print(var)
    return str(var) + "\n"

@app.route("/data")
def get_data():
    return jsonify(data)

@app.route("/latest_var")
def get_latest_var():
    return latest_var

@app.route("/chart")
def chart():
    return send_file("index.html")