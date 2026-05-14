# test_dogrib
"""
Before you run this, run:
  python main.py --input-instance-folder ../data/dogrib/ --output-folder ../results/dogrib/
    --ignitions --sim-years 1 --nsims 1 --finalGrid --weather rows --nweathers 1
    --Fire-Period-Length 1.0 --output-messages --ROS-CV 0.0 --seed 123 --stats
    --IgnitionRad 5 --grids
That will create the input files. The test then re-runs with --onlyProcessing
(skips the C++ call) and checks ForestGrid08.csv against the baseline.
"""
import csv
import unittest
import os.path
from cell2fire.utils.ParseInputs import make_parser
from cell2fire.Cell2FireC_class import *
import cell2fire

p = str(cell2fire.__path__)
l = p.find("'")
r = p.find("'", l+1)
cell2fire_path = p[l+1:r]
data_path = os.path.join(cell2fire_path, "..", "data")

result_Forest08 = os.path.join(cell2fire_path, "..", "results", "dogrib", "Grids", "Grids1", "ForestGrid08.csv")
baseline_Forest08 = os.path.join(cell2fire_path, "..", "tests", "baseline", "dogrib", "ForestGrid08.csv")


def _cmd_list():
    datadir = os.path.abspath(os.path.join(data_path, "dogrib"))
    resultsdir = os.path.abspath(os.path.join(data_path, "..", "results", "dogrib"))
    return ["--input-instance-folder", datadir,
            "--output-folder", resultsdir,
            "--ignitions",
            "--sim-years", "1",
            "--nsims", "1",
            "--finalGrid",
            "--weather", "rows",
            "--nweathers", "1",
            "--Fire-Period-Length", "1.0",
            "--output-messages",
            "--ROS-CV", "0.0",
            "--seed", "123",
            "--stats",
            "--IgnitionRad", "5",
            "--grids"]


class TestMain(unittest.TestCase):

    def test_forest_grid(self):
        parser = make_parser()
        args = parser.parse_args(_cmd_list() + ["--onlyProcessing"])
        env = Cell2FireC(args)
        env.stats()

        with open(result_Forest08) as result_file, open(baseline_Forest08) as baseline_file:
            for line1, line2 in zip(result_file, baseline_file):
                row1 = next(csv.reader([line1.strip().rstrip(',')]))
                row2 = next(csv.reader([line2.strip().rstrip(',')]))
                self.assertEqual(row1, row2, "ForestGrid08.csv does not match baseline")


if __name__ == "__main__":
    unittest.main()
