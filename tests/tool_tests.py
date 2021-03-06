import os
import subprocess
import unittest
import definitions as defs

class TestTool(unittest.TestCase):

    NETLIST_PATHS = os.path.join(defs.INSTALL_PREFIX, 'netlist-paths')

    def setUp(self):
        pass

    def run_np(self, args):
        command = [self.NETLIST_PATHS] + args
        proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if proc.returncode != 0:
            print('Error executing {}'.format(' '.join(proc.args)))
            print('Stdout:\n{}\nStderr:\n{}'.format(proc.stdout.decode('utf-8'),
                                                    proc.stderr.decode('utf-8')))
        return (proc.returncode, proc.stdout.decode('utf-8'))

    def test_netlist_paths_bin(self):
        self.assertTrue(os.path.exists(self.NETLIST_PATHS))

    def test_xml_output(self):
        test_path = os.path.join(defs.TEST_SRC_PREFIX, 'adder.sv')
        xml_path = os.path.join(defs.CURRENT_BINARY_DIR, 'adder.xml')
        if os.path.exists(xml_path):
            os.remove(xml_path)
        returncode, _ = self.run_np(['--compile', test_path, '--output', xml_path])
        self.assertEqual(returncode, 0)
        self.assertTrue(os.path.exists(xml_path))

    def test_adder(self):
        test_path = os.path.join(defs.TEST_SRC_PREFIX, 'adder.sv')
        returncode, _ = self.run_np(['--compile', test_path])
        self.assertEqual(returncode, 0)

    def test_dump_names(self):
        test_path = os.path.join(defs.TEST_SRC_PREFIX, 'counter.sv')
        returncode, stdout = self.run_np(['--compile', test_path, '--dump-names'])
        self.assertEqual(returncode, 0)
        self.assertEqual(len(stdout.split('\n')), 15)

    def test_dump_regs(self):
        test_path = os.path.join(defs.TEST_SRC_PREFIX, 'counter.sv')
        returncode, stdout = self.run_np(['--compile', test_path, '--dump-regs'])
        self.assertEqual(returncode, 0)
        self.assertEqual(len(stdout.split('\n')), 4)

    def test_dump_ports(self):
        test_path = os.path.join(defs.TEST_SRC_PREFIX, 'counter.sv')
        returncode, stdout = self.run_np(['--compile', test_path, '--dump-ports'])
        self.assertEqual(returncode, 0)
        self.assertEqual(len(stdout.split('\n')), 11)

    def test_dump_nets(self):
        test_path = os.path.join(defs.TEST_SRC_PREFIX, 'counter.sv')
        returncode, stdout = self.run_np(['--compile', test_path, '--dump-nets'])
        self.assertEqual(returncode, 0)
        self.assertEqual(len(stdout.split('\n')), 5)

    def test_dump_any_path(self):
        test_path = os.path.join(defs.TEST_SRC_PREFIX, 'multiple_paths.sv')
        returncode, _ = self.run_np(['--compile', test_path, '--from', 'in', '--to', 'out'])
        self.assertEqual(returncode, 0)

    def test_dump_all_paths(self):
        test_path = os.path.join(defs.TEST_SRC_PREFIX, 'multiple_paths.sv')
        returncode, _ = self.run_np(['--compile', test_path, '--from', 'in', '--to', 'out', '--all-paths'])
        self.assertEqual(returncode, 0)

    def test_dump_fan_out_paths(self):
        test_path = os.path.join(defs.TEST_SRC_PREFIX, 'counter.sv')
        returncode, _ = self.run_np(['--compile', test_path, '--from', 'counter.counter_q'])
        self.assertEqual(returncode, 0)

    def test_dump_fan_in_paths(self):
        test_path = os.path.join(defs.TEST_SRC_PREFIX, 'counter.sv')
        returncode, _ = self.run_np(['--compile', test_path, '--to', 'counter.counter_q'])
        self.assertEqual(returncode, 0)


if __name__ == '__main__':
    unittest.main()
