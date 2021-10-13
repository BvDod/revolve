import os
import csv

import pandas as pd
import matplotlib
import matplotlib.pyplot as plt
import numpy as np
import seaborn as sns


def datafolder_to_df(experiment_name):
    """ This function extracts the phenotype, behavioural, fitness and generation data from the files and returns a df """

    # Data input locations
    experiment_folder = f"data/{experiment_name}/"
    phenotype_folder = experiment_folder + "data_fullevolution/descriptors/"
    behave_folder = phenotype_folder + "behavioural/"
    generations_folder = experiment_folder + "generations/"
    fitness_csv = experiment_folder + "data_fullevolution/fitness.csv"


    # Dict that contains data about each "robot-id"
    robots = {}

    # First load description data
    ## Phenotype
    for file in os.listdir(phenotype_folder):
        if not file[-4:] == ".txt":
            continue
        robot_id = int(file.split("_")[2].split(".")[0])
        with open(phenotype_folder + file) as csvfile:
            reader = csv.reader(csvfile, delimiter=" ")
            descr_dict = {rows[0]:float(rows[1]) for rows in reader}
        robots[robot_id] = descr_dict

    ## Behavioural
    for file in os.listdir(behave_folder):
        if not file[-4:] == ".txt":
            continue
        robot_id = int(file.split("_")[2].split(".")[0])

        with open(behave_folder + file) as csvfile:
            reader = csv.reader(csvfile, delimiter=" ")
            behave_dict = {rows[0]:float(rows[1]) for rows in reader if len(rows) > 1}
        robots[robot_id].update(behave_dict)

    # Add correct generation(s) to each robot
    for folder in os.listdir(generations_folder):
        generation_id = int(folder.split("_")[1])
        with open(generations_folder + folder + "/identifiers.txt") as csvfile:
            reader = csv.reader(csvfile, delimiter=" ")
            for row in reader:
                if not "generations" in robots[int(row[0])]:
                    robots[int(row[0])]["generations"] = []
                robots[int(row[0])]["generations"].append(generation_id)

    # Add fitness to each robot
    with open(fitness_csv) as csvfile:
        reader = csv.reader(csvfile, delimiter=",")
        for row in reader:
            if not row[1] == "None":
                robots[int(row[0])]["fitness"] = float(row[1])

    robot_df = pd.DataFrame.from_dict(robots, orient="index").sort_index()

    return robot_df


def get_data_per_generation(df, data_label):
    """ This function extracts the proper *data label* values for each generation"""
    """ Note: this is needed because each robot can occur in multiple gens"""

    x, y = [], []
    df = df[df["generations"].notna()]
    for index, row in df.iterrows():
        for gen in row["generations"]:
            x.append(gen)
            y.append(row[data_label])
    return x, y


def get_mean_and_std_and_max_generation_wise(x, y):
    """ Returns mean and std for each generation"""

    generations = np.array(list(set(x)))
    mean_gens = np.empty_like(generations, dtype=float)
    std_gens = np.empty_like(generations, dtype=float)
    max_gens = np.empty_like(generations, dtype=float)

    for generation in generations:
        mean = np.mean([fitness for fitness, y_gen in zip(y,x) if y_gen == generation])
        std = np.std([fitness for fitness, y_gen in zip(y,x) if y_gen == generation])
        max = np.max([fitness for fitness, y_gen in zip(y,x) if y_gen == generation])
        mean_gens[generation] = mean
        std_gens[generation] = std
        max_gens[generation] = max

    return generations,mean_gens, std_gens, max_gens


def plot_generational_graph(generations, mean, std, max, label, figure_dir):
    """ Creates a line plot with accompanying std region """

    sns.set()
    plt.plot(generations, mean, 'b-', label="mean")
    plt.plot(generations, max, 'r-', label="max")
    plt.fill_between(generations, mean - std, mean + std, color='b', alpha=0.2)
    plt.xlabel("Generation")
    plt.ylabel(label)
    plt.title(f"Mean {label}")
    plt.legend()

    # Save png of plot to disc
    if not os.path.exists(figure_dir):
        os.makedirs(figure_dir)
    plt.savefig(f"{figure_dir}{label}.png")

    if only_save_mode:
        plt.clf()
    else:
        plt.show()      

def get_a_values(experiment_name):
    experiment_folder = f"data/{experiment_name}/"
    alpha_csv = experiment_folder + "data_fullevolution/alphas.csv"

    with open(alpha_csv) as csvfile:
        generations = []
        line_a = []
        height_a = []
        reader = csv.reader(csvfile, delimiter=",")
        for row in reader:
            generations.append(int(row[0]))
            line_a.append(float(row[1]))
            height_a.append(float(row[2]))

    return generations, line_a, height_a

def plot_curve(generations, line_a, height_a, figure_dir):
    plt.plot(generations, line_a, label="Line Fitness weight")
    plt.plot(generations, height_a, label="Height weight")

    plt.xlabel("Generation")
    plt.ylabel("alpha")
    plt.title("Alpha weight values")

    plt.legend()

    if not os.path.exists(figure_dir):
        os.makedirs(figure_dir)
    plt.savefig(f"{figure_dir}alpha_curve.png")

    if only_save_mode:
        plt.clf()
    else:
        plt.show()       

if __name__== "__main__":

    # Name of the experiment folder
    experiment_name = "default_experiment/1"

    only_save_mode = True

    # Returns all robots and their associated data in a df
    robot_df = datafolder_to_df(experiment_name)

    # To check out the data
    robot_df.to_csv('df_test.csv')

    # Convert to height in blocks
    block_height = 0.035
    robot_df["avg_z_in_blocks"] = robot_df["average_height"] / block_height

    # Save figures as png to this dir
    figure_dir = f"figures/{experiment_name}/"

    # Extracts generation, fitness pairs for each robot. (needed since each robot can occur > or < than 1 time in a generation)
    x, y = get_data_per_generation(robot_df, "fitness")
    # Calculates mean and std for each generation.
    generations, mean, std, max = get_mean_and_std_and_max_generation_wise(x, y)
    plot_generational_graph(generations, mean, std, max, "Fitness", figure_dir)

    x, y = get_data_per_generation(robot_df, "follow_line_fitness")
    generations, mean, std, max = get_mean_and_std_and_max_generation_wise(x, y)
    plot_generational_graph(generations, mean, std, max, "Distance traveled", figure_dir)

    x, y = get_data_per_generation(robot_df, "average_height")
    generations, mean, std, max = get_mean_and_std_and_max_generation_wise(x, y)
    plot_generational_graph(generations, mean, std, max, "Average height", figure_dir)

    x, y = get_data_per_generation(robot_df, "avg_z_in_blocks")
    generations, mean, std, max = get_mean_and_std_and_max_generation_wise(x, y)
    plot_generational_graph(generations, mean, std, max, "Average height (blocks)", figure_dir)

    gens, line_a, height_a = get_a_values(experiment_name)
    plot_curve(gens, line_a, height_a, figure_dir)