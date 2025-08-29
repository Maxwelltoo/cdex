import os
import csv

def process_csv_files():
    fields_dir = 'fields/'
    output_file = 'descriptors.csv'

    # Get all CSV files from the directory
    try:
        csv_files = [f for f in os.listdir(fields_dir) if f.endswith('.csv')]
    except FileNotFoundError:
        print(f"Error: Directory not found at '{fields_dir}'")
        return

    with open(output_file, 'w', newline='') as outfile:
        for file_name in csv_files:
            input_file_path = os.path.join(fields_dir, file_name)

            # Get the file name without extension for the first column
            base_name = os.path.splitext(file_name)[0]
            data_parts = [base_name]

            try:
                with open(input_file_path, 'r', newline='') as infile:
                    reader = csv.reader(infile)
                    for row in reader:
                        # Skip empty rows or rows starting with '#'
                        if not row or row[0].strip().startswith('#'):
                            continue
                        
                        # Ensure row has at least 3 columns
                        if len(row) >= 3:
                            name = row[1].strip()
                            type = row[2].strip()
                            data_parts.append(f"{name}:{type}")
            except FileNotFoundError:
                print(f"Warning: File not found '{input_file_path}', skipping.")
                continue
            except IndexError:
                print(f"Warning: Skipping row in '{input_file_path}' due to missing columns.")
                continue

            # Write the compressed line to the output file
            outfile.write(','.join(data_parts) + '\n')

    print(f"Successfully created '{output_file}'")

if __name__ == '__main__':
    process_csv_files()
