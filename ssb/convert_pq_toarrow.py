import os
import sys
import json
import pyarrow.parquet as pq
import pyarrow as pa

def get_num_distinct_values(table, column_name):
    return len(table[column_name].unique())

def extract_type_name(data_type):
    if pa.types.is_integer(data_type):
        return "int"
    elif pa.types.is_floating(data_type):
        return "float"
    elif pa.types.is_boolean(data_type):
        return "bool"
    elif pa.types.is_string(data_type):
        return "string"
    elif pa.types.is_date32(data_type):
        return "date"
    else:
        raise ValueError("Unknown data type")

def get_props(data_type):
    props = []
    if pa.types.is_integer(data_type):
        props.append(data_type.bit_width)
    elif pa.types.is_floating(data_type):
        props.append(data_type.bit_width)

    return props

def convert_arrow_to_parquet(arrow_folder, parquet_folder):
    # Create the destination folder if it doesn't exist
    os.makedirs(parquet_folder, exist_ok=True)

    # Get a list of all .arrow files in the source folder
    arrow_files = [f for f in os.listdir(arrow_folder) if f.endswith('.arrow')]

    # Convert each Arrow file to Parquet file
    for arrow_file in arrow_files:
        arrow_path = os.path.join(arrow_folder, arrow_file)
        parquet_file = arrow_file.replace('.arrow', '.parquet')
        parquet_path = os.path.join(parquet_folder, parquet_file)

        table = None

        # Read the Arrow file
        with pa.ipc.open_file(arrow_path) as reader:
            table = reader.read_all()

        # Modify the table schema to replace fixed size binary types with strings
        schema = table.schema
        for i, field in enumerate(schema):
            if isinstance(field.type, pa.FixedSizeBinaryType):
                schema = schema.set(i, pa.field(field.name, pa.string()))

        # Create a new table with the modified schema
        modified_table = pa.Table.from_pandas(table.to_pandas(), schema=schema)

        # Write the Parquet file
        pq.write_table(modified_table, parquet_path)

        print(f"Converted {arrow_file} to {parquet_file}")

def convert_parquet_to_arrow(parquet_folder, arrow_folder):
    # Create the destination folder if it doesn't exist
    os.makedirs(arrow_folder, exist_ok=True)

    # Get a list of all .parquet files in the source folder
    parquet_files = [f for f in os.listdir(parquet_folder) if f.endswith('.parquet')]

    # Convert each Parquet file to Arrow file
    for parquet_file in parquet_files:
        parquet_path = os.path.join(parquet_folder, parquet_file)
        arrow_file = parquet_file.replace('.parquet', '.arrow')
        arrow_path = os.path.join(arrow_folder, arrow_file)

        # Read the Parquet file
        table = pq.read_table(parquet_path)
        new_column_names = [column_name.lower() for column_name in table.column_names]
        table = table.rename_columns(new_column_names)

         # Write the Arrow file
        with pa.ipc.new_file(arrow_path, schema=table.schema) as writer:
            writer.write_table(table)

        # Write the metadata.json file
        metadata = table.schema
        metadata_path = arrow_path.replace('.arrow', '.metadata.json')

        # Prepare the JSON data
        json_data = {
            "columns": [],
            "num_rows": table.num_rows,
            "pkey": [metadata[0].name]
        }

        # Extract column information
        for column in metadata:
            column_info = {
                "distinct_values": get_num_distinct_values(table, column.name),
                "name": column.name,
                "type": {"base": extract_type_name(column.type),"nullable": column.nullable, "props": get_props(column.type)}
            }
            json_data["columns"].append(column_info)

        # Write the metadata.json file
        with open(metadata_path, 'w') as f:
            json.dump(json_data, f)

        print(f"Converted {parquet_file} to {arrow_file} with metadata")

if __name__ == "__main__":
    # Check if the correct number of command-line arguments are provided
    if len(sys.argv) != 3:
        print("Usage: python convert_pq_toarrow.py <arrow_folder> <parquet_folder>")
        sys.exit(1)

    # Get the command-line arguments
    arrow_folder = sys.argv[1]
    parquet_folder = sys.argv[2]

    # Convert Parquet files to Arrow files
    convert_parquet_to_arrow(parquet_folder, arrow_folder)

    # Convert Arrow files to Parquet files
    # convert_arrow_to_parquet(arrow_folder, parquet_folder)