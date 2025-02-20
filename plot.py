import pandas as pd
import matplotlib
import numpy as np
import scipy.interpolate as spi  # Step 1: Import SciPy


matplotlib.use("Agg")
import matplotlib.pyplot as plt
from datetime import datetime, timedelta

# Paramètres
COLORS = {
    "PM2.5": "#d62728",
    "PM1": "#2ca02c",
    "CO2": "#1f77b4",
    "TVOC": "#ff7f0e",
    "Temperature": "#e377c2",
    "Humidity": "#9467bd",
}


import pandas as pd
import matplotlib
import numpy as np
import scipy.interpolate as spi

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from datetime import datetime, timedelta

COLORS = {
    "PM2.5": "#d62728",
    "PM1": "#2ca02c",
    "CO2": "#1f77b4",
    "TVOC": "#ff7f0e",
    "Temperature": "#e377c2",
    "Humidity": "#9467bd",
}


def apply_spline(df, s=1000):  # Valeur initiale plus élevée
    """Spline robuste avec gestion des erreurs"""
    if df.empty or len(df) < 10:
        return df

    df_smoothed = pd.DataFrame(index=df.index)
    t_start = df.index[0].value // 1e9
    times = (df.index.view("int64") // 1e9 - t_start).astype(float)

    for col in df.columns:
        valid = df[col].notna()
        valid_data = df.loc[valid, col]

        if len(valid_data) < 4:
            df_smoothed[col] = df[col]
            continue

        try:
            # Calcul dynamique avec limites minimales
            duration = times[-1] - times[0]
            base_s = max(s * (60 / duration if duration > 0 else 1), 50)

            spl = spi.UnivariateSpline(times[valid], valid_data, s=base_s, ext="const")
            df_smoothed[col] = spl(times)

            # Remplacer les valeurs aberrantes
            df_smoothed[col] = np.clip(
                df_smoothed[col], valid_data.min() * 0.9, valid_data.max() * 1.1
            )
        except Exception as e:
            print(f"Fallback to linear interpolation for {col}: {str(e)}")
            df_smoothed[col] = df[col].interpolate(method="time")

    # Nettoyage final
    return df_smoothed.replace([np.inf, -np.inf], np.nan).ffill().bfill()


def detect_measurement_intervals(df, max_gap_min=15):
    """Détecte les intervalles de mesure continus"""
    gaps = df.index.to_series().diff().dt.total_seconds() / 60 > max_gap_min
    split_indices = np.where(gaps)[0]
    return np.split(df, split_indices)


def parse_data(filename):
    """Lit et traite les données brutes"""
    data = []
    current_date = datetime(2023, 1, 1).date()
    previous_time = datetime.min.time()

    with open(filename, "r", encoding="latin-1") as f:
        for line in f:
            if line.startswith("Time (UTC),lat,lon,Temperature"):
                continue
            parts = line.strip().split(",")
            if len(parts) != 10:
                continue

            try:
                t = datetime.strptime(parts[0], "%H:%M:%S").time()
                if t < previous_time:
                    current_date += timedelta(days=1)
                dt = datetime.combine(current_date, t)

                data.append(
                    {
                        "datetime": dt,
                        "PM2.5": float(parts[9]),
                        "PM1": float(parts[8]),
                        "TVOC": float(parts[5]),  # ppb
                        "CO2": float(parts[6]),  # ppm
                        "Temperature": float(parts[3]),
                        "Humidity": float(parts[4]),
                    }
                )
                previous_time = t
            except (ValueError, IndexError):
                continue

    df = pd.DataFrame(data).sort_values("datetime")
    df.set_index("datetime", inplace=True)
    return df


def remove_global_outliers(df, iqr_factor=1.5):
    """Supprime les valeurs aberrantes dans toutes les données avec méthode IQR"""
    if df.empty:
        return df

    # Calcul des bornes IQR pour chaque colonne
    Q1 = df.quantile(0.25)
    Q3 = df.quantile(0.75)
    IQR = Q3 - Q1

    lower_bound = Q1 - iqr_factor * IQR
    upper_bound = Q3 + iqr_factor * IQR

    # Masque combiné pour toutes les colonnes
    mask = (df >= lower_bound) & (df <= upper_bound)
    return df[mask.all(axis=1)].dropna()


def add_mean_lines(ax, series, color, unit):
    """Ajoute uniquement la valeur moyenne dans la légende"""
    mean = series.mean()
    # Crée un élément de légende personnalisé sans ligne visible
    ax.plot(
        [],
        [],
        color=color,
        linestyle=":",
        alpha=0.7,
        label=f"Moyenne: {mean:.1f}{unit}",
    )
    return mean


def plot_air_quality(df, df_ma, suffix=""):
    """Version corrigée avec initialisation correcte de ax2"""
    fig, ax1 = plt.subplots(figsize=(16, 7))

    # Création de ax2 IMMÉDIATEMENT APRÈS ax1
    ax2 = ax1.twinx()  # <-- Initialisation ajoutée ici

    df_ma_clean = df_ma

    try:
        # Calcul des limites AVEC vérification des NaN
        co2_min = max(df_ma["CO2"].min() * 0.95, 300) if not df_ma.empty else 300
        co2_max = df_ma["CO2"].max() * 1.05 if not df_ma.empty else 1000

        tvoc_min = max(df_ma["TVOC"].min() * 0.8, 0) if not df_ma.empty else 0
        tvoc_max = df_ma["TVOC"].max() * 1.2 if not df_ma.empty else 100

        # Configuration CO₂
        ax1.set_ylabel("CO₂ (ppm)", color=COLORS["CO2"])
        line_co2 = ax1.plot(df_ma.index, df_ma["CO2"], color=COLORS["CO2"], lw=1.5)
        co2_mean = add_mean_lines(ax1, df["CO2"], COLORS["CO2"], " ppm")

        # Configuration TVOC
        ax2.set_ylabel("TVOC (ppb)", color=COLORS["TVOC"])
        line_tvoc = ax2.plot(df_ma.index, df_ma["TVOC"], color=COLORS["TVOC"], lw=1.5)
        ax2.set_ylim(tvoc_min, tvoc_max)  # Maintenant ax2 est défini

        tvoc_min = 0  # TVOC ne peut être négatif
        tvoc_max = df_ma_clean["TVOC"].max() * 1.2

        # Configuration CO₂
        ax1.set_ylabel("CO₂ (ppm)", color=COLORS["CO2"])
        line_co2 = ax1.plot(df_ma.index, df_ma["CO2"], color=COLORS["CO2"], lw=1.5)
        co2_mean = add_mean_lines(ax1, df["CO2"], COLORS["CO2"], " ppm")

        # Configuration TVOC avec échelle dynamique
        ax2 = ax1.twinx()
        ax2.set_ylabel("TVOC (ppb)", color=COLORS["TVOC"])
        line_tvoc = ax2.plot(df_ma.index, df_ma["TVOC"], color=COLORS["TVOC"], lw=1.5)

        # Calcul des limites TVOC pour mieux voir les variations
        tvoc_min = df_ma["TVOC"].min() * 0.8
        tvoc_max = df_ma["TVOC"].max() * 1.2
        ax2.set_ylim(tvoc_min, tvoc_max)

        tvoc_mean = add_mean_lines(ax2, df["TVOC"], COLORS["TVOC"], " ppb")

        # Légende unifiée
        lines = line_co2 + line_tvoc
        labels = [f"CO₂ (moy: {co2_mean:.1f} ppm)", f"TVOC (moy: {tvoc_mean:.1f} ppb)"]
        ax1.legend(lines, labels, loc="upper left")

        plt.title(f"Qualité air - Période {suffix}", pad=20)
        fig.autofmt_xdate(rotation=45)
        plt.tight_layout()
        plt.savefig(f"air_quality_{suffix}.png", dpi=300)
        plt.close()
    except Exception as e:
        print(f"Erreur de tracé: {str(e)}")
        plt.close()
        return


# Modifier de la même façon plot_particulates et plot_climate :
def plot_particulates(df, df_ma, suffix=""):
    """Graphique PM avec échelles optimisées"""
    # Nettoyage des données
    df_clean = remove_global_outliers(df[["PM1", "PM2.5"]])
    if df_clean.empty:
        return

    df_ma_clean = df_ma

    # Configuration graphique
    fig, ax = plt.subplots(figsize=(16, 5))

    # Échelle dynamique avec limites réalistes
    pm_max = max(df_ma_clean["PM2.5"].max(), df_ma_clean["PM1"].max()) * 1.2
    ax.set_ylim(0, pm_max)  # Les PM ne peuvent être négatives

    # Courbes
    pm25_line = ax.plot(
        df_ma_clean.index, df_ma_clean["PM2.5"], color=COLORS["PM2.5"], lw=1.5
    )
    pm1_line = ax.plot(
        df_ma_clean.index, df_ma_clean["PM1"], color=COLORS["PM1"], lw=1.5
    )

    # Calcul des moyennes
    pm25_mean = df_clean["PM2.5"].mean()
    pm1_mean = df_clean["PM1"].mean()

    # Légende unifiée
    lines = pm25_line + pm1_line
    labels = [f"PM2.5 (moy: {pm25_mean:.1f} μg/m³)", f"PM1 (moy: {pm1_mean:.1f} μg/m³)"]
    ax.legend(lines, labels, loc="upper left")

    plt.title(f"Particules Fines - Période {suffix}")
    plt.savefig(f"particulates_{suffix}.png", dpi=300)
    plt.close()


def plot_climate(df, df_ma, suffix=""):
    """Graphique Temp/Hum avec échelles adaptées"""
    # Nettoyage des données
    df_clean = remove_global_outliers(df[["Temperature", "Humidity"]])
    if df_clean.empty:
        return

    df_ma_clean = df_ma

    # Configuration graphique
    fig, ax1 = plt.subplots(figsize=(16, 5))

    # Température (axe gauche)
    temp_min = df_ma_clean["Temperature"].min() - 2
    temp_max = df_ma_clean["Temperature"].max() + 2
    ax1.set_ylim(temp_min, temp_max)
    temp_line = ax1.plot(
        df_ma_clean.index,
        df_ma_clean["Temperature"],
        color=COLORS["Temperature"],
        lw=1.5,
    )

    # Humidité (axe droit)
    ax2 = ax1.twinx()
    ax2.set_ylim(0, 100)  # L'humidité est toujours entre 0-100%
    hum_line = ax2.plot(
        df_ma_clean.index, df_ma_clean["Humidity"], color=COLORS["Humidity"], lw=1.5
    )

    # Calcul des moyennes
    temp_mean = df_clean["Temperature"].mean()
    hum_mean = df_clean["Humidity"].mean()

    # Légende combinée
    lines = temp_line + hum_line
    labels = [
        f"Température (moy: {temp_mean:.1f}°C)",
        f"Humidité (moy: {hum_mean:.1f}%)",
    ]
    ax1.legend(lines, labels, loc="upper left")

    plt.title(f"Climat - Période {suffix}")
    plt.savefig(f"climate_{suffix}.png", dpi=300)
    plt.close()


# Execution
df = parse_data("SEQLOG00.TXT")
intervals = detect_measurement_intervals(df)

for i, interval in enumerate(intervals):
    cleaned_interval = remove_global_outliers(interval).dropna()
    cleaned_interval = cleaned_interval[
        ~cleaned_interval.index.duplicated(keep="first")
    ]
    if not cleaned_interval.empty:
        try:
            # Résolution du warning de dépréciation (1S -> 1s)
            df_preprocessed = cleaned_interval.resample("1s").asfreq()

            # Interpolation uniquement sur les colonnes numériques
            numeric_cols = df_preprocessed.select_dtypes(include=np.number).columns
            df_preprocessed[numeric_cols] = df_preprocessed[numeric_cols].interpolate(
                method="time"
            )

            # Sécurité supplémentaire contre les index dupliqués
            df_preprocessed = df_preprocessed[
                ~df_preprocessed.index.duplicated(keep="first")
            ]

            # Application du lissage
            df_spline = apply_spline(df_preprocessed, s=10)

            # Vérification finale des NaN/Inf
            df_spline.replace([np.inf, -np.inf], np.nan, inplace=True)
            df_spline.dropna(inplace=True)

            if not df_spline.empty:
                plot_air_quality(cleaned_interval, df_spline, suffix=f"period{i+1}")
                plot_particulates(cleaned_interval, df_spline, suffix=f"period{i+1}")
                plot_climate(cleaned_interval, df_spline, suffix=f"period{i+1}")
            else:
                print(f"Données lissées vides pour la période {i+1}")

        except ValueError as ve:
            print(f"Erreur de traitement période {i+1} : {str(ve)}")
            # Fallback aux données brutes nettoyées
            plot_air_quality(cleaned_interval, cleaned_interval, suffix=f"period{i+1}")
            plot_particulates(cleaned_interval, cleaned_interval, suffix=f"period{i+1}")
            plot_climate(cleaned_interval, cleaned_interval, suffix=f"period{i+1}")

    else:
        print(f"Aucune donnée valide pour la période {i+1}")

print("Graphiques générés pour les périodes :")
for i in range(len(intervals)):
    print(f"- Période {i+1} :")
    print(f"  air_quality_period{i+1}.png")
    print(f"  particulates_period{i+1}.png")
    print(f"  climate_period{i+1}.png")
