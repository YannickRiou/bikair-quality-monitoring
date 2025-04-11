import pandas as pd
import plotly.express as px
import folium
from folium.plugins import MarkerCluster
import plotly.io as pio
import argparse


def generate_timeseries_graphs(data):
    if data.empty:
        print("Aucune donnée pour générer les graphiques.")
        return None

    # Essayer de convertir la colonne Time en format datetime
    try:
        data["Time (UTC)"] = pd.to_datetime(
            data["Time (UTC)"], format="%H:%M:%S", errors="coerce"
        )
    except Exception as e:
        print(f"Erreur lors de la conversion de Time: {e}")
        return None

    # Liste des colonnes à tracer (en excluant 'Time' et 'AQI')
    columns_to_plot = ["Temperature", "Humidity", "TVOC", "CO2", "PM1", "PM2"]

    # Assurez-vous que les colonnes sont bien des données numériques
    for col in columns_to_plot:
        data[col] = pd.to_numeric(
            data[col], errors="coerce"
        )  # Convertir les colonnes en numériques, les erreurs deviennent NaN

    # Créer un graphique interactif pour chaque série temporelle
    plotly_graphs = []
    for col in columns_to_plot:
        # Créer un graphique interactif pour chaque série temporelle
        fig = px.line(data, x="Time (UTC)", y=col, title=f"Time Series: {col}")

        # Ajouter des titres et étiquettes
        fig.update_layout(
            xaxis_title="Time (UTC)", yaxis_title=col, template="plotly_dark"
        )

        # Sauvegarder le graphique comme HTML pour pouvoir l'intégrer plus tard
        graph_html = pio.to_html(fig, full_html=False)
        plotly_graphs.append(graph_html)

    return plotly_graphs


def generate_map(data, plotly_graphs):
    # Convert 'lat' and 'lon' columns to numeric, coercing errors to NaN
    data["lat"] = pd.to_numeric(data["lat"], errors="coerce")
    data["lon"] = pd.to_numeric(data["lon"], errors="coerce")

    # Drop rows with NaN values in 'lat' or 'lon'
    data = data.dropna(subset=["lat", "lon"])

    # Now you can safely compute the mean of lat and lon
    map_center = [data["lat"].mean(), data["lon"].mean()]
    folium_map = folium.Map(location=map_center, zoom_start=12)

    # Cluster des marqueurs
    marker_cluster = MarkerCluster().add_to(folium_map)

    for index, row in data.iterrows():
        if (
            row["lat"] != 0 and row["lon"] != 0
        ):  # Ignorer les points où lat et lon sont 0
            popup_content = f"""
                <strong>Time:</strong> {row['Time (UTC)']}<br>
                <strong>Temp:</strong> {row['Temperature']}°C<br>
                <strong>Humidity:</strong> {row['Humidity']}%<br>
                <strong>TVOC:</strong> {row['TVOC']} ppb<br>
                <strong>CO2:</strong> {row['CO2']} ppm<br>
                <strong>PM1:</strong> {row['PM1']} µg/m³<br>
                <strong>PM2:</strong> {row['PM2']} µg/m³<br>
                <strong>AQI:</strong> {row['AQI']}<br>
            """

            # Ajouter un marqueur
            folium.Marker([row["lat"], row["lon"]], popup=popup_content).add_to(
                marker_cluster
            )

    # Sauvegarder la carte dans un fichier HTML
    map_filename = "map.html"
    folium_map.save(map_filename)
    print(f"Carte sauvegardée sous {map_filename}")
    return map_filename


def generate_combined_html(plotly_graphs, map_filename):
    # Créer un fichier HTML qui contient à la fois la carte et les graphiques Plotly
    with open("combined_output.html", "w") as f:
        # Intégrer la carte
        with open(map_filename, "r") as map_file:
            map_html = map_file.read()

        # Ajouter la carte et les graphiques Plotly dans le fichier HTML
        f.write("<html><body>")
        f.write("<h1>Time Series Graphs</h1>")
        for graph_html in plotly_graphs:
            f.write(graph_html)  # Ajouter chaque graphique Plotly
        f.write("<h1>Map</h1>")
        f.write(map_html)  # Ajouter la carte
        f.write("</body></html>")

    print("Fichier HTML combiné généré sous 'combined_output.html'.")


def main(file_path):
    try:
        # Lire le fichier de données
        data = pd.read_csv(file_path)
    except Exception as e:
        print(f"Erreur lors de la lecture du fichier: {e}")
        return

    # Générer les graphiques
    plotly_graphs = generate_timeseries_graphs(data)
    if not plotly_graphs:
        print("Erreur lors de la génération des graphiques.")
        return

    # Générer la carte
    map_filename = generate_map(data, plotly_graphs)

    # Générer le fichier HTML combiné
    generate_combined_html(plotly_graphs, map_filename)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()

    parser.add_argument("file_path", type=str, help="Chemin vers le fichier de données")
    args = parser.parse_args()

    main(args.file_path)
