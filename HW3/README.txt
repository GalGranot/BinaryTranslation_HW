Golan Gershonowitz 208830257 golang@campus.technion.ac.il
Gal Granot 315681593 gal.granot@campus.technion.ac.il

Compilation:
1.	extract src from ex3.zip
2.	cd to src
3.	for compilation: make PIN_ROOT="<pin_path>" obj-intel64/ex3.cpp 
	where <pin_path> is the path of the installation of pin program.
	The .so file will be created in the relative path ./obj-intel64/ex3.so
4.	to RUN: cd to pin path
			run: ./pin -prof -t <path_to_so_file> -- <your command and parameters as usual>
				the output of the tool will be saved in the current dir, in the files "loop-output.csv" and "rtnFile.txt".
			only after running with -prof for the first time
			run: ./pin -inst -t <path_to_so_file> -- <your command and parameters as usual> # add more flags as you wish