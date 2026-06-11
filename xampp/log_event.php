<?php
header('Content-Type: application/json');
$conn = new mysqli('localhost', 'root', '', 'rfid_db');

$conn->query("CREATE TABLE IF NOT EXISTS event_log (
    id        INT AUTO_INCREMENT PRIMARY KEY,
    type      INT,
    uid       VARCHAR(8),
    user_name VARCHAR(50),
    result    TINYINT,
    message   VARCHAR(100),
    ts        TIMESTAMP DEFAULT CURRENT_TIMESTAMP
)");

$b = json_decode(file_get_contents('php://input'), true);
$stmt = $conn->prepare(
    "INSERT INTO event_log (type,uid,user_name,result,message) VALUES (?,?,?,?,?)"
);
$stmt->bind_param('ississ',
    $b['type'], $b['uid'], $b['user_name'], $b['result'], $b['message']
);
$stmt->execute();
echo json_encode(['success' => true]);
$conn->close();