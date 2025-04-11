import pandas as pd
import plotly.express as px
import folium
from folium.plugins import MarkerCluster
import plotly.io as pio
import argparse
import json


def load_json_lines(file_path):
    data = []
    with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            try:
                data.append(json.loads(line.strip()))
            except json.JSONDecodeError:
                continue  # Ignore la ligne invalide
    return pd.DataFrame(data)


def generate_timeseries_graphs(data):
    if data.empty:
        print("Aucune donnée pour générer les graphiques.")
        return None

    # Convertir l'heure
    if "time_utc" in data.columns:
        data["time_utc"] = pd.to_datetime(
            data["time_utc"], format="%H:%M:%S", errors="coerce"
        )

    # Colonnes numériques possibles
    numeric_cols = ["temperature", "humidity", "tvoc", "co2", "pm1", "pm2", "speed"]
    for col in numeric_cols:
        if col in data.columns:
            data[col] = pd.to_numeric(data[col], errors="coerce")

    plotly_graphs = []

    # Graphiques individuels
    for col in ["temperature", "humidity", "tvoc", "co2", "pm1", "pm2", "speed"]:
        if col in data.columns:
            fig = px.line(
                data, x="time_utc", y=col, title=f"Time Series: {col.upper()}"
            )
            fig.update_layout(
                xaxis_title="Time (UTC)",
                yaxis_title=col.upper(),
                template="plotly_dark",
            )
            plotly_graphs.append(pio.to_html(fig, full_html=False))

    # Graphique combiné
    combined_cols = [
        col for col in ["tvoc", "co2", "pm1", "pm2"] if col in data.columns
    ]
    if combined_cols:
        fig_combined = px.line(
            data, x="time_utc", y=combined_cols, title="TVOC / CO2 / PM1 / PM2"
        )
        fig_combined.update_layout(
            xaxis_title="Time (UTC)", yaxis_title="Valeurs", template="plotly_dark"
        )
        plotly_graphs.append(pio.to_html(fig_combined, full_html=False))

    return plotly_graphs


def generate_map(data):
    if "latitude" not in data.columns or "longitude" not in data.columns:
        print("Colonnes latitude/longitude manquantes pour la carte.")
        return None

    data["latitude"] = pd.to_numeric(data["latitude"], errors="coerce")
    data["longitude"] = pd.to_numeric(data["longitude"], errors="coerce")
    data = data.dropna(subset=["latitude", "longitude"])
    data = data[(data["latitude"] != 0) & (data["longitude"] != 0)]

    if data.empty:
        print("Pas de coordonnées GPS valides pour générer la carte.")
        return None

    map_center = [data["latitude"].mean(), data["longitude"].mean()]
    folium_map = folium.Map(location=map_center, zoom_start=12)
    marker_cluster = MarkerCluster().add_to(folium_map)

    for _, row in data.iterrows():
        popup_content = "<br>".join(
            f"<strong>{k}:</strong> {row[k]}"
            for k in [
                "time_utc",
                "temperature",
                "humidity",
                "tvoc",
                "co2",
                "pm1",
                "pm2",
            ]
            if k in row
        )
        folium.Marker([row["latitude"], row["longitude"]], popup=popup_content).add_to(
            marker_cluster
        )

    map_filename = "map.html"
    folium_map.save(map_filename)
    print(f"Carte sauvegardée sous {map_filename}")
    return map_filename


def generate_combined_html(plotly_graphs, map_filename):
    with open("combined_output.html", "w") as f:
        f.write("<html><body>")
        f.write("<h1>Time Series Graphs</h1>")
        for graph_html in plotly_graphs:
            f.write(graph_html)
        if map_filename:
            with open(map_filename, "r") as map_file:
                f.write("<h1>Map</h1>")
                f.write(map_file.read())
        f.write("</body></html>")
    print("Fichier HTML combiné généré sous 'combined_output.html'.")


def main(file_path):
    try:
        data = load_json_lines(file_path)
    except Exception as e:
        print(f"Erreur lors de la lecture du fichier JSON: {e}")
        return

    plotly_graphs = generate_timeseries_graphs(data)
    if not plotly_graphs:
        return

    map_filename = generate_map(data)
    generate_combined_html(plotly_graphs, map_filename)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("file_path", type=str, help="Chemin vers le fichier JSON")
    args = parser.parse_args()
    main(args.file_path)
