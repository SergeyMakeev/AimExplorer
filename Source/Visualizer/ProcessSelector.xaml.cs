using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;

namespace Visualizer
{
    /// <summary>
    /// Interaction logic for ProcessSelector.xaml
    /// </summary>
    public partial class ProcessSelector : Window
    {
        class ProcessDesc
        {
            public int Id { get; set; }
            public string Name { get; set; }
        }

        List<ProcessDesc> items = new List<ProcessDesc>();

        public ProcessSelector()
        {
            InitializeComponent();

            BuildProcessList();
        }

        void BuildProcessList()
        {
            items.Clear();
            Process[] processes = Process.GetProcesses();
            foreach (Process process in processes)
            {
                items.Add(new ProcessDesc { Id = process.Id, Name = process.ProcessName });
            }
            items.Sort((i1, i2) => i1.Name.CompareTo(i2.Name));
            listView.ItemsSource = items;
        }

        private void scanButton_Click(object sender, RoutedEventArgs e)
        {
            ProcessDesc item = listView.SelectedItem as ProcessDesc;
            if (item == null)
            {
                return;
            }

            string xmlName = "scan_" + item.Id + ".xml";

            Process ScanProcess = new Process();
            ScanProcess.StartInfo.UseShellExecute = true;
            ScanProcess.StartInfo.FileName = "MemoryScan.exe";
            ScanProcess.StartInfo.Arguments = item.Id + " " + xmlName;
            ScanProcess.StartInfo.CreateNoWindow = true;
            ScanProcess.Start();

            while (!ScanProcess.HasExited && ScanProcess.Responding)
            {
                Thread.Sleep(100);
            }

            if (ScanProcess.ExitCode != 0)
            {
                MessageBox.Show(this, "MemoryScan Failed! Code: " + ScanProcess.ExitCode, "Error");
                return;
            }

            MessageBox.Show(this, "Successfully generated " + xmlName, "Done!");
        }

        private void BroadPhaseProcess_Exited(object sender, EventArgs e)
        {
            throw new NotImplementedException();
        }
    }
}
