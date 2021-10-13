import numpy as np

def linear_curve(current_gen, total_gen, start_line_a, end_line_a):
    line_a = start_line_a + ((current_gen/total_gen) * (end_line_a- start_line_a))
    height_a = 1 - line_a

    return line_a, height_a

def step_down_curve(current_gen, total_gen, start_line_a, end_line_a):
    steps = 8
    step_size = total_gen/steps
    factor = (1-end_line_a)**(1/(steps-1))

    height_a = (1-start_line_a) * factor**int(current_gen/step_size)
    line_a = 1 - height_a

    return line_a, height_a

def exponentional_curve(current_gen, total_gen, start_line_a, end_line_a):
    k = - np.log(1-end_line_a)/total_gen
    
    height_a = (1-start_line_a) * np.exp(-k*current_gen)
    line_a = 1 - height_a

    return line_a, height_a
