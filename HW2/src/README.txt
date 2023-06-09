Golan Gershonowitz 208830257 golang@campus.technion.ac.il
Gal Granot 315681593 gal.granot@campus.technion.ac.il

Compilation:
1.	extract src from ex2.zip
2.	cd to src
3.	for compilation: make PIN_ROOT="<pin_path>" obj-intel64/ex2.cpp 
	where <pin_path> is the path of the installation of pin program.
	The .so file will be created in the relative path ./obj-intel64/ex2.so
4.	to RUN: cd to pin path
			run ./pin -t <path_to_so_file> -- <your command and parameters as usual>
5.	the output of the tool will be saved in the current dir, in the file "loop-output.csv".