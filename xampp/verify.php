<?php
header('Content-Type: application/json');
$conn = new mysqli('localhost', 'root', '', 'rfid_db');

$body = json_decode(file_get_contents('php://input'), true);
$uid  = strtoupper($conn->real_escape_string($body['uid'] ?? ''));

$res  = $conn->query("SELECT id, name FROM users WHERE uid='$uid' LIMIT 1");
$row  = $res->fetch_assoc();

echo json_encode([
    'authorized' => $row ? true : false,
    'user_id'    => $row ? (int)$row['id'] : 0,
    'user_name'  => $row ? $row['name'] : ''
]);
$conn->close();